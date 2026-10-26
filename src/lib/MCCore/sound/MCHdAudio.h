#pragma once

#include "platform/MCAudio.h"

class MCFlac;

/// <summary>
/// Port-only: PREFS "HDAudio": 0 plays the game's own sounds; 1 the HD audio pack's music, effects and voices where it
/// has them; 2 the same with the pack's orchestral music (its stereo music where it has no orchestral version).
/// </summary>
extern int GHdAudio;

/// <summary>GHdAudio's values.</summary>
enum MCHdAudioSetting : int
{
    HdAudioOff = 0,
    HdAudioOn = 1,
    HdAudioOrchestral = 2,
};

/// <summary>
/// Optional audio beside the art in the HD pack folder (<see cref="MCHdArt::PackFolder"/>): <c>audio/audio.cfg</c>
/// (<c>[HDAudioPack]</c>, <c>Format=1</c>) marks the pack; <c>audio/music/stereo|orchestral/MUSICnn.flac</c> replace the
/// music by name and <c>audio/samples/&lt;SampleKey&gt;.flac</c> the effects and voices by their PCM. A replacement is
/// used only when <see cref="GHdAudio"/> is on and its length matches the original's; otherwise the original plays.
/// </summary>
class MCHdAudio
{
public:
    /// <summary>A replacement sample, decoded: its format and its signed 16-bit little-endian PCM.</summary>
    struct Decoded
    {
        MCSoundFormat Format;
        std::vector<uint8_t> Pcm;
    };

    /// <summary>The pack's replacement for the music file <paramref name="original"/> (MUSICnn only), or none.</summary>
    static std::optional<std::filesystem::path> Music(std::string_view original);
    /// <summary>Canonical FNV-1a key: rate u32, channels u16, bits u16 (little endian), then the original PCM bytes.</summary>
    static std::string SampleKey(std::span<const uint8_t> wave);
    /// <summary>
    /// The pack's replacement for the wave image <paramref name="wave"/>, decoded, or none. Decoded replacements (and
    /// the waves that have none) are remembered, the least recently used going past a budget, so a sound played again
    /// costs a hash of its PCM and a lookup.
    /// </summary>
    static std::shared_ptr<const Decoded> Sample(std::span<const uint8_t> wave);
    /// <summary>Whether <paramref name="flac"/> lasts as long as the original, to the nearest output frame.</summary>
    static bool MatchesLength(const MCFlac& flac, uint64_t sourceFrames, uint32_t sourceRate);
    /// <summary>Whether the pack folder has HD audio: an audio.cfg of a format this code reads.</summary>
    static bool PackReadable();
    /// <summary>Forgets what was found in the pack (the tests change a pack's files while it is in use).</summary>
    static void Forget();
};
