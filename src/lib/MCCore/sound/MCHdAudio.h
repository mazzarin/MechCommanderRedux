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
/// <c>audio/ambience/rain.flac</c> is HD weather's rain, a loop beyond the original's sounds.
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
    /// <summary>
    /// The pack's rain loop (<c>audio/ambience/rain.flac</c>), or none. It goes with HD weather, an option of its own, so
    /// it is there whatever <see cref="GHdAudio"/> is set to.
    /// </summary>
    static std::optional<std::filesystem::path> RainLoop();
    /// <summary>The pack's thunder (<c>audio/ambience/thunder.flac</c>), or none; there whatever HD audio is, as the rain loop.</summary>
    static std::optional<std::filesystem::path> Thunder();
    /// <summary>
    /// The wave image <paramref name="wave"/>'s identity among the decoded replacements: xxHash's XXH3 of its PCM,
    /// seeded with its format, which a caller may keep (the sound system keeps each loaded bite's). None for an image
    /// that isn't a PCM wave.
    /// </summary>
    static std::optional<uint64_t> SampleId(std::span<const uint8_t> wave);
    /// <summary>
    /// The name of the wave's replacement in the pack: its canonical key, a 64-bit FNV-1a of rate u32, channels u16,
    /// bits u16 (little endian), then the original PCM bytes, in 16 hex digits (empty for an image that isn't a wave).
    /// It is worked out once a wave, to find its file.
    /// </summary>
    static std::string SampleKey(std::span<const uint8_t> wave);
    /// <summary>
    /// The pack's replacement for the wave image <paramref name="wave"/>, decoded, or none. Decoded replacements (and
    /// the waves that have none) are remembered, the least recently used going past a budget, so a sound played again
    /// costs its <see cref="SampleId"/> (none when the caller kept it, as <paramref name="id"/>) and a lookup; its
    /// player shares the decoded samples, without a copy.
    /// </summary>
    static std::shared_ptr<const Decoded> Sample(std::span<const uint8_t> wave,
                                                 std::optional<uint64_t> id = std::nullopt);
    /// <summary>
    /// Decodes the replacements of <paramref name="waves"/> on a thread of their own, ahead of their first plays, and
    /// keeps them, beyond the budget, while the pack is in use: the sound file's effects, Betty's samples and the
    /// radio's static, which play the moment they are asked for. A wave played before its turn is decoded as it plays
    /// (or, when the thread is on it, waits for it): the same sound either way. Nothing with HD audio off.
    /// </summary>
    static void Prepare(std::vector<std::vector<uint8_t>> waves);
    /// <summary>Whether there is nothing to <see cref="Prepare"/>: HD audio is off, or it was for the pack in use.</summary>
    static bool Prepared();
    /// <summary>
    /// Starts decoding <paramref name="wave"/>'s replacement on that thread, ahead of its play (a radio message's speech,
    /// behind its static); remembered as one played.
    /// </summary>
    static void Prefetch(std::span<const uint8_t> wave);
    /// <summary>Waits till the thread has decoded every wave handed to <see cref="Prepare"/> and
    /// <see cref="Prefetch"/> (the end of a mission's loading).</summary>
    static void FinishPreparing();
    /// <summary>Whether <paramref name="flac"/> lasts as long as the original, to the nearest output frame.</summary>
    static bool MatchesLength(const MCFlac& flac, uint64_t sourceFrames, uint32_t sourceRate);
    /// <summary>Whether the pack folder has HD audio: an audio.cfg of a format this code reads.</summary>
    static bool PackReadable();
    /// <summary>Whether the pack has orchestral music (<c>audio/music/orchestral</c>), for ORCHESTRAL to offer.</summary>
    static bool HasOrchestral();
    /// <summary>Forgets what was found in the pack (the tests change a pack's files while it is in use).</summary>
    static void Forget();
};
