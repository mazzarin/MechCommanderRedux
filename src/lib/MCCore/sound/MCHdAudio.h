#pragma once

class MCFlac;

/// <summary>Optional audio beside the art in the user/game HD folder, independent of the HDArt preference.</summary>
class MCHdAudio
{
public:
    /// <summary>Format 1 audio.cfg enables the pack. MusicSet is stereo (default), orchestral, or original.</summary>
    static std::optional<std::filesystem::path> Music(std::string_view original);
    /// <summary>Canonical FNV-1a key: rate u32, channels u16, bits u16 (little endian), then original PCM bytes.</summary>
    static std::string SampleKey(std::span<const uint8_t> wave);
    static std::optional<std::filesystem::path> Sample(std::span<const uint8_t> wave);
    /// <summary>Refuses a replacement whose duration differs by more than half one output sample frame.</summary>
    static bool MatchesLength(const MCFlac& flac, uint64_t sourceFrames, uint32_t sourceRate);
};
