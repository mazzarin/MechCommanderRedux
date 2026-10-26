#pragma once

#include "sprite/MCHdArt.h"

/// <summary>
/// Port-only: a movie's HD frames from the HD art pack (<see cref="MCHdArt::FindMovie"/>): AV1 in an IVF file, a frame
/// for each of the Smacker movie's, decoded one after another by dav1d (src/lib/dav1d-1.5.4). The original keeps playing
/// for its sound, timing and palette; each of its frames is shown from the next HD one, as made.
/// </summary>
class MCHdMovie
{
public:
    /// <summary>Opens the movie at <paramref name="path"/>, reading its header.</summary>
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
    /// The next frame over the original's (<paramref name="original"/>: <paramref name="width"/> x
    /// <paramref name="height"/> palette indices, <paramref name="pitch"/> bytes a row): the AV1 frame's colours, and
    /// the original's indices.
    /// </summary>
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> Next(std::span<const uint8_t> original,
                                                                           int32_t width, int32_t height,
                                                                           int32_t pitch);

    /// <summary>Decodes every frame left (the pack check's).</summary>
    /// <returns>How many there were.</returns>
    std::expected<uint32_t, std::string> Validate();

private:
    MCHdMovie();
    struct Impl;
    std::unique_ptr<Impl> _Impl;
    int32_t _Width = 0;
    int32_t _Height = 0;
    uint32_t _Frames = 0;
};
