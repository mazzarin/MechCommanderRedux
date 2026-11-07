#pragma once

#include "sprite/MCHdArt.h"

/// <summary>
/// Port-only (HD movies): how a frame's Y, U and V make its colours, as SDL's own conversion makes them
/// (src/lib/SDL-release-3.4.12/src/video/yuv2rgb: yuv_rgb_internal.h's YUV2RGB, in 6-bit fixed point): red is
/// <c>(Y - YShift) * YFactor + (V - 128) * VRed</c>, green adds <c>(U - 128) * UGreen + (V - 128) * VGreen</c> to the
/// same Y part, blue <c>(U - 128) * UBlue</c>, each then made a byte as SDL's <c>clampU8</c> makes it.
/// </summary>
struct MCYuvFactors
{
    int32_t YShift = 0;
    int32_t YFactor = 0;
    int32_t VRed = 0;
    int32_t UGreen = 0;
    int32_t VGreen = 0;
    int32_t UBlue = 0;
};

/// <summary>
/// Port-only: a movie's HD frames from the HD art pack (<see cref="MCHdArt::FindMovie"/>): AV1 in an IVF file, a frame
/// for each of the Smacker movie's, decoded in order by dav1d (src/lib/dav1d-1.5.4) on a thread of its own, a few
/// frames ahead of the one shown. The original keeps playing for its sound, timing and palette; each of its frames is
/// shown from the next HD one, as made: its Y, U and V planes, which the GPU turns into colours
/// (<see cref="MCHdArt::ColorFormat::Yuv420"/>).
/// </summary>
class MCHdMovie
{
public:
    /// <summary>Opens the movie at <paramref name="path"/>, reading its header, and starts decoding it.</summary>
    static std::expected<std::unique_ptr<MCHdMovie>, std::string> Open(const std::filesystem::path& path);
    ~MCHdMovie();
    MCHdMovie(const MCHdMovie&) = delete;
    MCHdMovie& operator=(const MCHdMovie&) = delete;

    /// <summary>The frames' width and height, from the file's header.</summary>
    int32_t Width() const { return _Width; }
    int32_t Height() const { return _Height; }
    /// <summary>The frames the file's header counts.</summary>
    uint32_t Frames() const { return _Frames; }

    /// <summary>
    /// The HD pixels a pixel of a <paramref name="width"/> x <paramref name="height"/> original takes, when the frames
    /// are that size scaled by a whole number.
    /// </summary>
    std::optional<int32_t> ScaleOver(int32_t width, int32_t height) const;

    /// <summary>
    /// How long <see cref="Next"/> waits for a frame after the first: a display frame at 60 Hz, so the wait doesn't
    /// show. One not decoded by then has used up the frames ahead (the machine decodes the movie slower than it plays),
    /// and the original, which plays underneath throughout, shows on from that frame. With the C code alone on an M4
    /// Pro's efficiency cores (about three times slower), no frame of the opening waits this long.
    /// </summary>
    static constexpr std::chrono::milliseconds MaxWait{17};

    /// <summary>
    /// The next frame over the original's (<paramref name="original"/>: <paramref name="width"/> x
    /// <paramref name="height"/> palette indices, <paramref name="pitch"/> bytes a row): the AV1 frame's planes
    /// (<see cref="MCHdArt::ColorFormat::Yuv420"/>, in a colour space <see cref="Factors"/> knows), and the original's
    /// indices. Waits for it while the decoding is behind: the first as long as it takes, the others up
    /// to <paramref name="wait"/>, then fails and stops the decoding (it is slower than the movie plays).
    /// </summary>
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> Next(std::span<const uint8_t> original,
                                                                           int32_t width, int32_t height, int32_t pitch,
                                                                           std::chrono::milliseconds wait = MaxWait);

    /// <summary>Decodes every frame left (the pack check's).</summary>
    /// <returns>How many there were.</returns>
    std::expected<uint32_t, std::string> Validate();

    /// <summary>
    /// How SDL turns <paramref name="colorspace"/>'s YUV into colours (the GPU's movie conversion does the same): none
    /// for one it can't (only BT.601, BT.709 and full-range BT.2020).
    /// </summary>
    static std::optional<MCYuvFactors> Factors(SDL_Colorspace colorspace);

    /// <summary>
    /// <paramref name="frame"/>'s colours (<see cref="MCHdArt::ColorFormat::Yuv420"/>) as RGBA8 rows, made on the CPU
    /// by SDL (<c>SDL_ConvertPixelsAndColorspace</c>), as the frames were before the GPU made them: what the GPU's
    /// colours are checked against.
    /// </summary>
    static std::expected<std::vector<uint8_t>, std::string> Rgba(const MCHdArt::Frame& frame);

private:
    MCHdMovie();
    struct Impl;
    std::unique_ptr<Impl> _Impl;
    int32_t _Width = 0;
    int32_t _Height = 0;
    uint32_t _Frames = 0;
};
