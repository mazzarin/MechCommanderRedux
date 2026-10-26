#pragma once

#include "platform/MCWin32Defs.h"

class MCFile;
class MCFlac;

/// <summary>Where a sound resource's samples come from.</summary>
enum class MCSoundResourceType : int32_t
{
    /// <summary>A wave file, read whole into memory.</summary>
    File = 0,
    /// <summary>A wave file image the caller has in memory (the resource keeps a copy).</summary>
    Memory = 1,
    /// <summary>A wave file streamed from disk as it plays (music).</summary>
    Stream = 2,
};

/// <summary>
/// A wave: a file loaded whole, a caller's memory image, or a file streamed from disk. Knows its format, where its
/// samples are, and for a stream how to read and rewind them. The <see cref="MCSoundRenderer"/> owns every resource.
/// </summary>
/// <remarks>Original source: <c>game os\sound renderer\sound resource.cpp</c>.</remarks>
class MCSoundResource
{
public:
    /// <summary>
    /// A memory resource of the caller's RIFF WAVE image, copied (the original referred to the caller's memory, which
    /// had to outlive it; a resource is only read when it is played, right after it is made). Fatal when it isn't one.
    /// </summary>
    explicit MCSoundResource(std::span<const uint8_t> image);
    /// <summary>A file resource (read whole) or a stream resource (opened for reading as it plays). Fatal on a file
    /// that can't be opened or isn't a PCM wave.</summary>
    MCSoundResource(MCSoundResourceType type, std::string_view fileName);
    /// <summary>Closes the stream.</summary>
    ~MCSoundResource();

    MCSoundResource(const MCSoundResource&) = delete;
    MCSoundResource& operator=(const MCSoundResource&) = delete;

    /// <summary>
    /// Reads up to <paramref name="bytes"/> of stream; at the end, a looping stream rewinds and reads on, otherwise
    /// the rest is filled with silence.
    /// </summary>
    /// <param name="depth">How deep the looping read has recursed; Fatal above 3.</param>
    /// <returns>The buffer's size.</returns>
    uint32_t Read(std::span<uint8_t> buffer, bool loop, int32_t depth = 1);
    /// <summary>Seeks the stream back to its data.</summary>
    void Rewind();

    /// <summary>Where the samples come from.</summary>
    MCSoundResourceType Type = MCSoundResourceType::File;
    /// <summary>The data chunk in the image (empty for a stream).</summary>
    std::span<const uint8_t> WaveData() const
    {
        return std::span(_Image)
            .subspan(std::min(_WaveOffset, _Image.size()))
            .first(std::min<size_t>(WaveSize, _Image.size() - std::min(_WaveOffset, _Image.size())));
    }

    /// <summary>The data chunk's size.</summary>
    uint32_t WaveSize = 0;
    /// <summary>The format: read from the image's "fmt " chunk, or from a stream's file.</summary>
    tWAVEFORMATEX Format{};
    /// <summary>A stream's length in ms.</summary>
    uint32_t DurationMs = 0;

private:
    /// <summary>Reads the whole file into memory and finds its format and data. Fatal when it can't open it.
    /// </summary>
    void LoadFile();
    /// <summary>Walks the image's RIFF WAVE chunks for "fmt " and "data". Fatal when it isn't one.</summary>
    void ReadWaveInfo();
    /// <summary>
    /// Opens the file as a stream: checks RIFF/WAVE and PCM, reads the format, finds the data chunk, and works out
    /// the duration. Fatal on a bad file.
    /// </summary>
    void OpenStream();
    /// <summary>The HD audio pack's replacement for the wave, when there is one (<see cref="MCHdAudio::Sample"/>).</summary>
    void TryHdSample();
    /// <summary>
    /// The HD audio pack's music for the stream, when it has some as long as the original's
    /// <paramref name="sourceFrames"/> and not cut short; the original's file is closed then.
    /// </summary>
    void TryHdStream(uint64_t sourceFrames);
    /// <summary>Read for the HD music: as the wave's, silence past the end unless it loops.</summary>
    void ReadFlac(std::span<uint8_t> buffer, bool loop);

    /// <summary>The file name, for a file or stream.</summary>
    std::string _FileName;
    /// <summary>A file or memory resource's whole wave image.</summary>
    std::vector<uint8_t> _Image;
    /// <summary>Where the data chunk starts in the image.</summary>
    size_t _WaveOffset = 0;
    /// <summary>A stream's open file.</summary>
    std::unique_ptr<MCFile> _Stream;
    /// <summary>A stream's data chunk position in the file.</summary>
    int32_t _DataStart = 0;
    /// <summary>The HD music's decoder, when it plays in place of the stream's file.</summary>
    std::unique_ptr<MCFlac> _Flac;
};
