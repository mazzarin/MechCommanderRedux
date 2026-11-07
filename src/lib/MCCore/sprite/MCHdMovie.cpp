#include "stdafx.h"
#include "sprite/MCHdMovie.h"
#include "main/MCTelemetry.h"
#include "platform/MCUtf8Path.h"

#include <condition_variable>
#include <dav1d/dav1d.h>

namespace
{
    /// <summary>
    /// An IVF file: a header (its signature, version, header size, codec, width, height, rate, scale, frame count),
    /// then each frame's size and timestamp before its bytes, all little-endian.
    /// </summary>
    constexpr size_t IvfHeaderBytes = 32;
    constexpr size_t IvfFrameHeaderBytes = 12;
    constexpr std::string_view IvfSignature = "DKIF";
    constexpr std::string_view Av1Codec = "AV01";
    constexpr size_t IvfCodecAt = 8;
    constexpr size_t IvfHeaderSizeAt = 6;
    constexpr size_t IvfWidthAt = 12;
    constexpr size_t IvfHeightAt = 14;
    constexpr size_t IvfFramesAt = 24;
    /// <summary>
    /// The largest frame read, in bytes and in pixels a side: far over any movie's, but a broken file can't have the
    /// game allocate gigabytes.
    /// </summary>
    constexpr uint32_t MaxFrameBytes = 64 * 1024 * 1024;
    constexpr int32_t MaxSide = 8192;
    /// <summary>
    /// dav1d's threads, and the frames they decode at once (which the frames ahead allow: none is wanted the moment
    /// it is sent). With the pack's movies made for it (tools/seedvr2/pack_movies.py: fast decoding, 4 tile columns),
    /// the opening (2560x1392) takes 1.7 ms a frame on average on an M4 Pro with the arm64 assembly and 4.2 with the C
    /// code alone (as on Windows), against 6.2 and 18.8 two threads took a frame at a time, before; 32 with the C code
    /// alone on its efficiency cores (about three times slower). Its slowest frames still take several times as long.
    /// </summary>
    constexpr int DecoderThreads = 4;
    constexpr int FramesInFlight = 4;
    /// <summary>
    /// The frames decoded ahead of the one shown, which take up the slow frames' time: 0.8 seconds of a movie, 64 MB of
    /// the opening's planes. Played in real time with the C code alone on the M4 Pro's efficiency cores (about three
    /// times slower), the opening never waits for a frame; decoded as it was shown, two threads a frame at a time, one
    /// frame in nine held the game past the 67 ms a frame shows, on the performance cores.
    /// </summary>
    constexpr size_t FramesAhead = 12;
    constexpr size_t RgbaBytes = 4;

    /// <summary>
    /// A factor of SDL's YUV conversion as its table writes it (yuv_rgb_internal.h's V: 6-bit fixed point, rounded up
    /// from a half; a negative one is a positive one's negation).
    /// </summary>
    constexpr int32_t Fixed(double value)
    {
        constexpr double FixedOne = 64.0;
        constexpr double Half = 0.5;
        return static_cast<int16_t>(value * FixedOne + Half);
    }

    /// <summary>SDL's table's rows (its YCbCrType), and a limited range's black, where its Y starts.</summary>
    enum class YuvRow : uint8_t
    {
        Bt601Full,
        Bt601Limited,
        Bt709Full,
        Bt709Limited,
        Bt2020Full,
        Rows
    };

    constexpr int32_t Black = 16;

    /// <summary>SDL's table (YUV2RGB): ITU-T T.871 for full-range BT.601, then the ITU-R ones.</summary>
    constexpr std::array<MCYuvFactors, static_cast<size_t>(YuvRow::Rows)> SdlYuvTable = {{
        {0, Fixed(1.0), Fixed(1.402), -Fixed(0.3441), -Fixed(0.7141), Fixed(1.772)},
        {Black, Fixed(1.1644), Fixed(1.596), -Fixed(0.3918), -Fixed(0.813), Fixed(2.0172)},
        {0, Fixed(1.0), Fixed(1.581), -Fixed(0.1881), -Fixed(0.47), Fixed(1.8629)},
        {Black, Fixed(1.1644), Fixed(1.7927), -Fixed(0.2132), -Fixed(0.5329), Fixed(2.1124)},
        {0, Fixed(1.0), Fixed(1.4760), -Fixed(0.1647), -Fixed(0.5719), Fixed(1.8832)},
    }};

    /// <summary>SDL's factors for <paramref name="row"/>.</summary>
    constexpr MCYuvFactors RowOf(YuvRow row)
    {
        return SdlYuvTable[static_cast<size_t>(row)];
    }

    uint16_t Le16(std::span<const uint8_t> bytes, size_t at)
    {
        return static_cast<uint16_t>(bytes[at] | bytes[at + 1] << 8);
    }

    uint32_t Le32(std::span<const uint8_t> bytes, size_t at)
    {
        return static_cast<uint32_t>(Le16(bytes, at)) | static_cast<uint32_t>(Le16(bytes, at + 2)) << 16;
    }

    bool HasText(std::span<const uint8_t> bytes, size_t at, std::string_view text)
    {
        return std::ranges::equal(bytes.subspan(at, text.size()), text,
                                  [](uint8_t byte, char letter) { return byte == static_cast<uint8_t>(letter); });
    }

    void Log(void*, const char* format, va_list arguments)
    {
        SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN, format, arguments);
    }

    /// <summary>How the frame's YUV maps to RGB, as its sequence header says (BT.709 when it doesn't).</summary>
    SDL_Colorspace ColorspaceOf(const Dav1dSequenceHeader& header)
    {
        const bool full = header.color_range != 0;

        switch (header.mtrx)
        {
            case DAV1D_MC_BT470BG:
            case DAV1D_MC_BT601:
                return full ? SDL_COLORSPACE_BT601_FULL : SDL_COLORSPACE_BT601_LIMITED;
            case DAV1D_MC_BT2020_NCL:
                return full ? SDL_COLORSPACE_BT2020_FULL : SDL_COLORSPACE_BT2020_LIMITED;
            default:
                return full ? SDL_COLORSPACE_BT709_FULL : SDL_COLORSPACE_BT709_LIMITED;
        }
    }

    /// <summary>A decoded picture, given back to dav1d when done with.</summary>
    struct Picture
    {
        Dav1dPicture Value{};

        Picture() = default;
        Picture(const Picture&) = delete;
        Picture& operator=(const Picture&) = delete;
        ~Picture() { dav1d_picture_unref(&Value); }
    };

    /// <summary>A frame decoded ahead: its three planes, one after another (MCHdArt::ColorFormat::Yuv420).</summary>
    struct Decoded
    {
        std::vector<uint8_t> Planes;
        /// <summary>How its YUV maps to RGB.</summary>
        SDL_Colorspace Colorspace = SDL_COLORSPACE_UNKNOWN;
    };
}

struct MCHdMovie::Impl
{
    std::ifstream File;
    std::string Name;
    /// <summary>The movie's number, which its frames carry (MCHdArt::Frame::Movie).</summary>
    uint64_t Id = MCHdArt::NewFrameId();
    Dav1dContext* Decoder = nullptr;
    /// <summary>The frame read last, until dav1d has taken all of it.</summary>
    Dav1dData Pending{};
    bool FileDone = false;
    /// <summary>Why the file can't be read on, given once the frames dav1d still decodes are out.</summary>
    std::optional<std::string> ReadError;

    /// <summary>Guards what follows, which <see cref="Worker"/> fills and the frames shown take.</summary>
    std::mutex Lock;
    /// <summary>Told when a frame is decoded or taken, and when the decoding ends.</summary>
    std::condition_variable_any Changed;
    /// <summary>The frames decoded and not yet taken, in order: at most <see cref="FramesAhead"/>.</summary>
    std::deque<Decoded> Ready;
    /// <summary>Why the decoding stopped, after <see cref="Ready"/>'s frames, if not at the file's end.</summary>
    std::optional<std::string> Error;
    /// <summary>Set when every frame has been decoded (or <see cref="Error"/>).</summary>
    bool Ended = false;
    /// <summary>The frames <see cref="MCHdMovie::Next"/> has taken (it waits for the first however long).</summary>
    uint32_t Taken = 0;
    /// <summary>Decodes the frames ahead, from <see cref="MCHdMovie::Open"/> until the movie is closed.</summary>
    std::jthread Worker;

    ~Impl()
    {
        // The worker uses the decoder until it stops, a frame's decoding at most.
        Worker.request_stop();

        if (Worker.joinable())
        {
            Worker.join();
        }

        dav1d_data_unref(&Pending);
        dav1d_close(&Decoder);
    }

    /// <summary>Reads the file's next frame into <see cref="Pending"/>.</summary>
    /// <returns>False at the file's end.</returns>
    std::expected<bool, std::string> ReadFrame()
    {
        std::array<uint8_t, IvfFrameHeaderBytes> header;
        File.read(reinterpret_cast<char*>(header.data()), header.size());

        if (File.gcount() == 0 && File.eof())
        {
            return false;
        }

        if (static_cast<size_t>(File.gcount()) != header.size())
        {
            return std::unexpected(std::format("{} is cut short in a frame's header", Name));
        }

        const uint32_t size = Le32(header, 0);

        if (size == 0 || size > MaxFrameBytes)
        {
            return std::unexpected(std::format("{} has a frame of {} bytes", Name, size));
        }

        uint8_t* const data = dav1d_data_create(&Pending, size);

        if (data == nullptr)
        {
            return std::unexpected(std::format("{}: no memory for a frame of {} bytes", Name, size));
        }

        File.read(reinterpret_cast<char*>(data), size);

        if (static_cast<size_t>(File.gcount()) != size)
        {
            dav1d_data_unref(&Pending);
            return std::unexpected(std::format("{} is cut short in a frame", Name));
        }

        return true;
    }

    /// <summary>The picture dav1d gives back next, into <paramref name="picture"/>.</summary>
    /// <returns>Whether it gave one: false once it has none left.</returns>
    std::expected<bool, std::string> GetPicture(Dav1dPicture& picture) const
    {
        const int got = dav1d_get_picture(Decoder, &picture);

        if (got != 0 && got != DAV1D_ERR(EAGAIN))
        {
            return std::unexpected(std::format("{}: dav1d couldn't decode a frame (error {})", Name, got));
        }

        return got == 0;
    }

    /// <summary>Decodes the next picture into <paramref name="picture"/>.</summary>
    /// <returns>False when there are no more.</returns>
    std::expected<bool, std::string> NextPicture(Dav1dPicture& picture)
    {
        // Each frame is sent before a picture is asked for, as dav1d's own player does: asked twice with nothing sent
        // between, dav1d gives back the frames it decodes at once, one by one until it has none, and only then takes
        // more.
        while (!FileDone || Pending.sz > 0)
        {
            if (Pending.sz == 0)
            {
                const std::expected<bool, std::string> read = ReadFrame();

                if (!read || !*read)
                {
                    ReadError = read ? std::nullopt : std::optional(read.error());
                    FileDone = true;
                    break;
                }
            }

            if (const int sent = dav1d_send_data(Decoder, &Pending); sent < 0 && sent != DAV1D_ERR(EAGAIN))
            {
                return std::unexpected(std::format("{}: dav1d refused a frame (error {})", Name, sent));
            }

            const std::expected<bool, std::string> got = GetPicture(picture);

            if (!got || *got)
            {
                return got;
            }
        }

        // The file's end, or a frame that can't be read: asked with nothing sent, dav1d gives back the frames it still
        // decodes, one by one; asked then, it has none left.
        const std::expected<bool, std::string> got = GetPicture(picture);

        if (!got || *got || !ReadError.has_value())
        {
            return got;
        }

        return std::unexpected(*ReadError);
    }

    /// <summary>Checks <paramref name="picture"/> is a frame this can show: the header's size, 8-bit 4:2:0.</summary>
    std::expected<void, std::string> Check(const Dav1dPicture& picture, int32_t width, int32_t height) const
    {
        if (picture.p.w != width || picture.p.h != height)
        {
            return std::unexpected(
                std::format("{} has a {}x{} frame in a {}x{} movie", Name, picture.p.w, picture.p.h, width, height));
        }

        if (picture.p.layout != DAV1D_PIXEL_LAYOUT_I420 || picture.p.bpc != 8 || picture.seq_hdr == nullptr)
        {
            return std::unexpected(std::format("{}'s frames aren't 8-bit 4:2:0", Name));
        }

        return {};
    }

    /// <summary>
    /// Decodes the next frame, <paramref name="width"/> x <paramref name="height"/>, into <paramref name="decoded"/>.
    /// </summary>
    /// <returns>False when there are no more.</returns>
    std::expected<bool, std::string> DecodeNext(Decoded& decoded, int32_t width, int32_t height)
    {
        Picture picture;
        const std::expected<bool, std::string> got = NextPicture(picture.Value);

        if (!got || !*got)
        {
            return got;
        }

        if (const std::expected<void, std::string> fits = Check(picture.Value, width, height); !fits)
        {
            return std::unexpected(fits.error());
        }

        // The frame keeps the three planes one after another (IYUV); dav1d keeps them apart, each with its own stride.
        const auto lumaWidth = static_cast<size_t>(width);
        const auto lumaHeight = static_cast<size_t>(height);
        const size_t chromaWidth = (lumaWidth + 1) / 2;
        const size_t chromaHeight = (lumaHeight + 1) / 2;
        decoded.Planes.resize(lumaWidth * lumaHeight + 2 * chromaWidth * chromaHeight);
        decoded.Colorspace = ColorspaceOf(*picture.Value.seq_hdr);
        uint8_t* out = decoded.Planes.data();
        const auto copyPlane = [&](int plane, size_t rowBytes, size_t rows)
        {
            const auto* in = static_cast<const uint8_t*>(picture.Value.data[plane]);
            const auto stride = static_cast<size_t>(picture.Value.stride[plane == 0 ? 0 : 1]);

            for (size_t row = 0; row < rows; ++row, out += rowBytes)
            {
                std::memcpy(out, in + row * stride, rowBytes);
            }
        };

        copyPlane(0, lumaWidth, lumaHeight);
        copyPlane(1, chromaWidth, chromaHeight);
        copyPlane(2, chromaWidth, chromaHeight);
        return true;
    }

    /// <summary>
    /// <see cref="Worker"/>'s work: decodes frame after frame while fewer than <see cref="FramesAhead"/> wait, to the
    /// file's end, a frame that fails, or <paramref name="stop"/>.
    /// </summary>
    void Decode(std::stop_token stop, int32_t width, int32_t height)
    {
        while (true)
        {
            {
                std::unique_lock lock(Lock);

                if (!Changed.wait(lock, stop, [this] { return Ready.size() < FramesAhead; }) || stop.stop_requested())
                {
                    // Stopped (the movie fell behind, or is closing): a frame asked for after finds the end.
                    Ended = true;
                    Changed.notify_all();
                    return;
                }
            }

            Decoded decoded;
            const std::expected<bool, std::string> made = DecodeNext(decoded, width, height);
            const bool more = made.value_or(false);
            {
                const std::lock_guard lock(Lock);

                if (more)
                {
                    Ready.push_back(std::move(decoded));
                }
                else
                {
                    Error = made ? std::nullopt : std::optional(made.error());
                    Ended = true;
                }
            }

            Changed.notify_all();

            if (!more)
            {
                return;
            }
        }
    }

    /// <summary>
    /// The next frame decoded, waiting for it while the worker is behind: up to <paramref name="within"/>, when given.
    /// </summary>
    /// <returns>None at the movie's end.</returns>
    std::expected<std::optional<Decoded>, std::string> Take(std::optional<std::chrono::milliseconds> within)
    {
        std::unique_lock lock(Lock);
        const auto due = [this] { return !Ready.empty() || Ended; };

        if (!within.has_value())
        {
            Changed.wait(lock, due);
        }
        else if (!Changed.wait_for(lock, *within, due))
        {
            return std::unexpected(
                std::format("{} fell behind: a frame wasn't decoded in {} ms", Name, within->count()));
        }

        if (Ready.empty())
        {
            if (Error.has_value())
            {
                return std::unexpected(*Error);
            }

            return std::nullopt;
        }

        Decoded next = std::move(Ready.front());
        Ready.pop_front();
        lock.unlock();
        Changed.notify_all();
        return next;
    }
};

MCHdMovie::MCHdMovie() : _Impl(std::make_unique<Impl>())
{
}

MCHdMovie::~MCHdMovie() = default;

auto MCHdMovie::Open(const std::filesystem::path& path) -> std::expected<std::unique_ptr<MCHdMovie>, std::string>
{
    auto movie = std::unique_ptr<MCHdMovie>(new MCHdMovie());
    Impl& impl = *movie->_Impl;
    impl.Name = MCFileSystem::ToUtf8(path.filename());
    impl.File.open(path, std::ios::binary);
    std::array<uint8_t, IvfHeaderBytes> header;
    impl.File.read(reinterpret_cast<char*>(header.data()), header.size());

    if (static_cast<size_t>(impl.File.gcount()) != header.size() || !HasText(header, 0, IvfSignature))
    {
        return std::unexpected(std::format("{} isn't an IVF file", impl.Name));
    }

    if (!HasText(header, IvfCodecAt, Av1Codec))
    {
        return std::unexpected(std::format("{} doesn't hold AV1", impl.Name));
    }

    const uint16_t headerBytes = Le16(header, IvfHeaderSizeAt);
    movie->_Width = Le16(header, IvfWidthAt);
    movie->_Height = Le16(header, IvfHeightAt);
    movie->_Frames = Le32(header, IvfFramesAt);

    if (headerBytes < IvfHeaderBytes || movie->_Width == 0 || movie->_Height == 0 || movie->_Width > MaxSide ||
        movie->_Height > MaxSide)
    {
        return std::unexpected(std::format("{}'s header is broken ({} bytes, {}x{})", impl.Name, headerBytes,
                                           movie->_Width, movie->_Height));
    }

    impl.File.seekg(headerBytes);
    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    settings.n_threads = DecoderThreads;
    settings.max_frame_delay = FramesInFlight;
    settings.frame_size_limit = static_cast<unsigned>(MaxSide) * MaxSide;
    settings.logger = {nullptr, Log};

    if (const int opened = dav1d_open(&impl.Decoder, &settings); opened < 0)
    {
        return std::unexpected(std::format("{}: dav1d didn't start (error {})", impl.Name, opened));
    }

    // From here the worker alone reads the file and uses the decoder.
    impl.Worker = std::jthread([&impl, width = movie->_Width, height = movie->_Height](std::stop_token stop)
                               { impl.Decode(stop, width, height); });
    return movie;
}

auto MCHdMovie::ScaleOver(int32_t width, int32_t height) const -> std::optional<int32_t>
{
    if (width <= 0 || height <= 0 || _Width % width != 0 || _Height % height != 0 || _Width / width != _Height / height)
    {
        return std::nullopt;
    }

    return _Width / width;
}

auto MCHdMovie::Next(std::span<const uint8_t> original, int32_t width, int32_t height, int32_t pitch,
                     std::chrono::milliseconds wait)
    -> std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string>
{
    Impl& impl = *_Impl;
    const std::optional<int32_t> scale = ScaleOver(width, height);

    if (!scale)
    {
        return std::unexpected(std::format("{}'s {}x{} frames aren't a {}x{} movie's scaled by a whole number",
                                           impl.Name, _Width, _Height, width, height));
    }

    if (pitch < width || original.size() < static_cast<size_t>(height - 1) * pitch + width)
    {
        return std::unexpected(std::format("{}: the original's {}x{} frame is given in {} bytes", impl.Name, width,
                                           height, original.size()));
    }

    std::expected<std::optional<Decoded>, std::string> decoded =
        impl.Take(impl.Taken == 0 ? std::nullopt : std::optional(wait));
    ++impl.Taken;

    if (!decoded)
    {
        // The movie is shown no more: its decoding stops after the frame it is on, rather than decoding frames ahead
        // that nothing takes while the original plays. Port (personal): the reports count it (main/MCTelemetry.h).
        impl.Worker.request_stop();
        MCTelemetry::MovieFellBehind();
    }

    if (!decoded || !decoded->has_value())
    {
        return std::unexpected(decoded ? std::format("{} has no more frames", impl.Name) : decoded.error());
    }

    Decoded& planes = **decoded;

    if (!Factors(planes.Colorspace).has_value())
    {
        return std::unexpected(std::format("{}'s frames are in a colour space that can't be shown (SDL's {:#x})",
                                           impl.Name, static_cast<uint32_t>(planes.Colorspace)));
    }

    // The planes as decoded: the GPU makes the colours (MCVulkanRenderer's movie pictures).
    auto frame = std::make_shared<MCHdArt::Frame>();
    frame->Id = MCHdArt::NewFrameId();
    frame->Width = _Width;
    frame->Height = _Height;
    frame->Scale = *scale;
    frame->Format = MCHdArt::ColorFormat::Yuv420;
    frame->Colors = std::move(planes.Planes);
    frame->Colorspace = planes.Colorspace;
    frame->Movie = impl.Id;

    frame->Indices.resize(static_cast<size_t>(width) * height * MCHdArt::IndexBytes);

    for (int32_t y = 0; y < height; ++y)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            const size_t at = (static_cast<size_t>(y) * width + x) * MCHdArt::IndexBytes;
            frame->Indices[at] = original[static_cast<size_t>(y) * pitch + x];
            frame->Indices[at + 1] = MCHdArt::HasIndex;
        }
    }

    return frame;
}

auto MCHdMovie::Validate() -> std::expected<uint32_t, std::string>
{
    uint32_t frames = 0;

    while (true)
    {
        const std::expected<std::optional<Decoded>, std::string> decoded = _Impl->Take(std::nullopt);

        if (!decoded)
        {
            return std::unexpected(decoded.error());
        }

        if (!decoded->has_value())
        {
            return frames;
        }

        ++frames;
    }
}

auto MCHdMovie::Factors(SDL_Colorspace colorspace) -> std::optional<MCYuvFactors>
{
    // As SDL picks its table's row (SDL_yuv.c's GetYUVConversionType).
    const bool limited = SDL_ISCOLORSPACE_LIMITED_RANGE(colorspace);

    if (SDL_ISCOLORSPACE_MATRIX_BT601(colorspace))
    {
        return RowOf(limited ? YuvRow::Bt601Limited : YuvRow::Bt601Full);
    }

    if (SDL_ISCOLORSPACE_MATRIX_BT709(colorspace))
    {
        return RowOf(limited ? YuvRow::Bt709Limited : YuvRow::Bt709Full);
    }

    if (SDL_ISCOLORSPACE_MATRIX_BT2020_NCL(colorspace) && SDL_ISCOLORSPACE_FULL_RANGE(colorspace))
    {
        return RowOf(YuvRow::Bt2020Full);
    }

    return std::nullopt;
}

auto MCHdMovie::Rgba(const MCHdArt::Frame& frame) -> std::expected<std::vector<uint8_t>, std::string>
{
    const auto width = static_cast<size_t>(frame.Width);
    const auto height = static_cast<size_t>(frame.Height);
    const size_t planes = width * height + 2 * ((width + 1) / 2) * ((height + 1) / 2);

    if (frame.Format != MCHdArt::ColorFormat::Yuv420 || frame.Colors.size() != planes)
    {
        return std::unexpected(std::format("a {}x{} frame of {} bytes isn't a movie's planes", frame.Width,
                                           frame.Height, frame.Colors.size()));
    }

    std::vector<uint8_t> colors(width * height * RgbaBytes);

    // Opaque, so the colours are premultiplied as they are.
    if (!SDL_ConvertPixelsAndColorspace(frame.Width, frame.Height, SDL_PIXELFORMAT_IYUV, frame.Colorspace, 0,
                                        frame.Colors.data(), frame.Width, SDL_PIXELFORMAT_RGBA32, SDL_COLORSPACE_SRGB,
                                        0, colors.data(), frame.Width * static_cast<int>(RgbaBytes)))
    {
        return std::unexpected(
            std::format("SDL couldn't convert a {}x{} frame ({})", frame.Width, frame.Height, SDL_GetError()));
    }

    return colors;
}
