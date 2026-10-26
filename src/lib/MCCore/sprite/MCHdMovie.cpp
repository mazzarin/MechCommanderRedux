#include "stdafx.h"
#include "sprite/MCHdMovie.h"

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
    /// dav1d's threads: two decode the largest movie (2560x1392) at about 50 frames a second on an M4 Pro, where it
    /// plays at 15.
    /// </summary>
    constexpr int DecoderThreads = 2;
    constexpr size_t RgbaBytes = 4;

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
}

struct MCHdMovie::Impl
{
    std::ifstream File;
    std::string Name;
    Dav1dContext* Decoder = nullptr;
    /// <summary>The frame read last, until dav1d has taken all of it.</summary>
    Dav1dData Pending{};
    bool FileDone = false;
    /// <summary>A frame's three planes, one after another, as SDL converts them (reused frame to frame).</summary>
    std::vector<uint8_t> Planes;

    ~Impl()
    {
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

    /// <summary>Decodes the next picture into <paramref name="picture"/>.</summary>
    /// <returns>False when there are no more.</returns>
    std::expected<bool, std::string> NextPicture(Dav1dPicture& picture)
    {
        while (true)
        {
            const int got = dav1d_get_picture(Decoder, &picture);

            if (got == 0)
            {
                return true;
            }

            if (got != DAV1D_ERR(EAGAIN))
            {
                return std::unexpected(std::format("{}: dav1d couldn't decode a frame (error {})", Name, got));
            }

            // dav1d wants more data: the next frame, or none at the end (it has given every picture back then).
            if (Pending.sz == 0)
            {
                if (FileDone)
                {
                    return false;
                }

                const std::expected<bool, std::string> read = ReadFrame();

                if (!read)
                {
                    return std::unexpected(read.error());
                }

                FileDone = !*read;
                continue;
            }

            if (const int sent = dav1d_send_data(Decoder, &Pending); sent < 0 && sent != DAV1D_ERR(EAGAIN))
            {
                return std::unexpected(std::format("{}: dav1d refused a frame (error {})", Name, sent));
            }
        }
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
};

MCHdMovie::MCHdMovie() : _Impl(std::make_unique<Impl>())
{
}

MCHdMovie::~MCHdMovie() = default;

auto MCHdMovie::Open(const std::filesystem::path& path) -> std::expected<std::unique_ptr<MCHdMovie>, std::string>
{
    auto movie = std::unique_ptr<MCHdMovie>(new MCHdMovie());
    Impl& impl = *movie->_Impl;
    impl.Name = path.filename().string();
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
    // A picture for each frame sent, as soon as it is: the movie is shown frame by frame as the original plays.
    settings.max_frame_delay = 1;
    settings.frame_size_limit = static_cast<unsigned>(MaxSide) * MaxSide;
    settings.logger = {nullptr, Log};

    if (const int opened = dav1d_open(&impl.Decoder, &settings); opened < 0)
    {
        return std::unexpected(std::format("{}: dav1d didn't start (error {})", impl.Name, opened));
    }

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

auto MCHdMovie::Next(std::span<const uint8_t> original, int32_t width, int32_t height, int32_t pitch)
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

    Picture picture;
    const std::expected<bool, std::string> decoded = impl.NextPicture(picture.Value);

    if (!decoded || !*decoded)
    {
        return std::unexpected(decoded ? std::format("{} has no more frames", impl.Name) : decoded.error());
    }

    if (const std::expected<void, std::string> fits = impl.Check(picture.Value, _Width, _Height); !fits)
    {
        return std::unexpected(fits.error());
    }

    // SDL converts the three planes one after another (IYUV); dav1d keeps them apart, each with its own stride.
    const auto lumaWidth = static_cast<size_t>(_Width);
    const auto lumaHeight = static_cast<size_t>(_Height);
    const size_t chromaWidth = (lumaWidth + 1) / 2;
    const size_t chromaHeight = (lumaHeight + 1) / 2;
    impl.Planes.resize(lumaWidth * lumaHeight + 2 * chromaWidth * chromaHeight);
    uint8_t* out = impl.Planes.data();
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

    auto frame = std::make_shared<MCHdArt::Frame>();
    frame->Id = MCHdArt::NewFrameId();
    frame->Width = _Width;
    frame->Height = _Height;
    frame->Scale = *scale;
    frame->Format = MCHdArt::ColorFormat::Rgba8;
    frame->Colors.resize(lumaWidth * lumaHeight * RgbaBytes);

    // Opaque, so the colours are premultiplied as they are.
    if (!SDL_ConvertPixelsAndColorspace(_Width, _Height, SDL_PIXELFORMAT_IYUV, ColorspaceOf(*picture.Value.seq_hdr), 0,
                                        impl.Planes.data(), _Width, SDL_PIXELFORMAT_RGBA32, SDL_COLORSPACE_SRGB, 0,
                                        frame->Colors.data(), _Width * static_cast<int>(RgbaBytes)))
    {
        return std::unexpected(std::format("{}: SDL couldn't convert a frame ({})", impl.Name, SDL_GetError()));
    }

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
        Picture picture;
        const std::expected<bool, std::string> decoded = _Impl->NextPicture(picture.Value);

        if (!decoded)
        {
            return std::unexpected(decoded.error());
        }

        if (!*decoded)
        {
            return frames;
        }

        if (const std::expected<void, std::string> fits = _Impl->Check(picture.Value, _Width, _Height); !fits)
        {
            return std::unexpected(fits.error());
        }

        ++frames;
    }
}
