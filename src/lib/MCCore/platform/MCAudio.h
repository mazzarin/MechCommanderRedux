#pragma once

struct MCAudioCore;
class MCAudioDevice;

/// <summary>The PCM format of a sound buffer or stream, as a WAVEFORMATEX with wFormatTag 1 (PCM) describes it.</summary>
struct MCSoundFormat
{
    /// <summary>Sample frames per second (DirectSound accepted 100-100000).</summary>
    uint32_t Rate = 22050;
    /// <summary>1 or 2; stereo is interleaved left, right.</summary>
    uint16_t Channels = 1;
    /// <summary>8 (unsigned, silence 0x80) or 16 (signed little-endian).</summary>
    uint16_t Bits = 16;

    /// <summary>Bytes per sample frame (nBlockAlign).</summary>
    uint32_t BlockAlign() const { return static_cast<uint32_t>(Channels) * (Bits / 8u); }
    /// <summary>Bytes per second (nAvgBytesPerSec).</summary>
    uint32_t BytesPerSecond() const { return Rate * BlockAlign(); }
    /// <summary>Whether the format is one the mixer plays.</summary>
    bool IsValid() const
    {
        return (Channels == 1 || Channels == 2) && (Bits == 8 || Bits == 16) && Rate >= 100 && Rate <= 200000;
    }
};

/// <summary>A RIFF WAVE file taken apart: its format and where its sample data is.</summary>
struct MCWaveData
{
    MCSoundFormat Format;
    /// <summary>The <c>data</c> chunk, inside the span that was parsed.</summary>
    std::span<const uint8_t> Samples;
};

/// <summary>
/// One DirectSound secondary buffer's worth of behaviour: a block of PCM that plays once or loops, with a play
/// cursor, volume, pan and frequency, mixed by <see cref="MCAudio"/>.
/// </summary>
/// <remarks>
/// <para>The methods are the <c>IDirectSoundBuffer</c> calls the original sound renderer made (vtable slots in
/// parentheses): <see cref="GetCurrentPosition"/> (0x10), <see cref="GetFrequency"/> (0x20),
/// <see cref="GetStatus"/> (0x24), <see cref="Lock"/> (0x2c), <see cref="Play"/> (0x30),
/// <see cref="SetCurrentPosition"/> (0x34), <see cref="SetVolume"/> (0x3c), <see cref="SetPan"/> (0x40),
/// <see cref="SetFrequency"/> (0x44), <see cref="Stop"/> (0x48), <see cref="Unlock"/> (0x4c); releasing the
/// buffer (0x08) is dropping the last <c>shared_ptr</c>, which stops it.</para>
/// <para>Every method is thread-safe. As with DirectSound, the bytes a <see cref="Lock"/> hands out are written
/// without a lock: write only the part the play cursor isn't in (the streaming code's
/// <c>GetCurrentPosition</c>-to-last-write gap).</para>
/// </remarks>
class MCSoundBuffer
{
    /// <summary>Only <see cref="MCAudio::CreateBuffer"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>GetStatus bit: the buffer is playing (DSBSTATUS_PLAYING).</summary>
    static constexpr uint32_t StatusPlaying = 0x1;
    /// <summary>GetStatus bit: the buffer is playing and loops (DSBSTATUS_LOOPING).</summary>
    static constexpr uint32_t StatusLooping = 0x4;

    /// <summary>The quietest volume, silence (DSBVOLUME_MIN), in hundredths of a decibel.</summary>
    static constexpr int32_t VolumeMin = -10000;
    /// <summary>Full volume (DSBVOLUME_MAX).</summary>
    static constexpr int32_t VolumeMax = 0;
    /// <summary>Only the left speaker (DSBPAN_LEFT): the right one is attenuated by 100 dB.</summary>
    static constexpr int32_t PanLeft = -10000;
    /// <summary>Only the right speaker (DSBPAN_RIGHT).</summary>
    static constexpr int32_t PanRight = 10000;

    /// <summary>
    /// A locked region: up to two pieces, the second one (empty otherwise) when the region wraps round the end. The
    /// pieces are the buffer's own bytes, valid until <see cref="Unlock"/>.
    /// </summary>
    struct MCLockedRegion
    {
        std::span<uint8_t> First;
        std::span<uint8_t> Second;
    };

    /// <summary>A silent buffer of <paramref name="bytes"/> bytes in <paramref name="format"/>, mixed by
    /// <paramref name="core"/>.</summary>
    MCSoundBuffer(Key, std::shared_ptr<MCAudioCore> core, const MCSoundFormat& format, uint32_t bytes);
    /// <summary>A buffer holding a copy of <paramref name="pcm"/>'s whole sample frames.</summary>
    MCSoundBuffer(Key, std::shared_ptr<MCAudioCore> core, const MCSoundFormat& format, std::span<const uint8_t> pcm);
    /// <summary>
    /// A buffer playing <paramref name="pcm"/>'s whole sample frames where they are, kept while it lives; the first
    /// <see cref="Lock"/> takes a copy of its own.
    /// </summary>
    MCSoundBuffer(Key, std::shared_ptr<MCAudioCore> core, const MCSoundFormat& format,
                  std::shared_ptr<const std::vector<uint8_t>> pcm);

    ~MCSoundBuffer();
    MCSoundBuffer(const MCSoundBuffer&) = delete;
    MCSoundBuffer& operator=(const MCSoundBuffer&) = delete;

    /// <summary>The buffer's format.</summary>
    const MCSoundFormat& Format() const { return _Format; }

    /// <summary>The buffer's size in bytes (dwBufferBytes).</summary>
    uint32_t Size() const { return static_cast<uint32_t>(Bytes().size()); }

    /// <summary>
    /// Hands out <paramref name="bytes"/> bytes from <paramref name="offset"/> for writing, split in two where they
    /// wrap round the end of the buffer (<c>Lock</c>). A request larger than the buffer is cut to its size. A buffer
    /// playing shared samples copies them first.
    /// </summary>
    MCLockedRegion Lock(uint32_t offset, uint32_t bytes);

    /// <summary>Ends a <see cref="Lock"/> (<c>Unlock</c>): publishes the writes to the mixer thread.</summary>
    void Unlock(const MCLockedRegion& region);

    /// <summary>Copies <paramref name="data"/> in at <paramref name="offset"/>, wrapping, as Lock + copy + Unlock.</summary>
    void Write(uint32_t offset, std::span<const uint8_t> data);

    /// <summary>
    /// Starts playing from the play cursor (<c>Play(0, 0, flags)</c>); <paramref name="looping"/> is DSBPLAY_LOOPING.
    /// Playing a buffer that already plays only changes whether it loops.
    /// </summary>
    void Play(bool looping);

    /// <summary>Stops, leaving the play cursor where it is (<c>Stop</c>).</summary>
    void Stop();

    /// <summary>
    /// <see cref="StatusPlaying"/> | <see cref="StatusLooping"/> as they apply (<c>GetStatus</c>). A buffer that
    /// plays once stops by itself at its end, with its play cursor back at 0.
    /// </summary>
    uint32_t GetStatus() const;

    /// <summary>Whether the buffer plays.</summary>
    bool IsPlaying() const { return (GetStatus() & StatusPlaying) != 0; }

    /// <summary>
    /// The play cursor and the write cursor, byte offsets in the buffer (<c>GetCurrentPosition</c>). The write cursor
    /// is where it is safe to write from: a little ahead of the play cursor, as the mixer has already read up to it.
    /// </summary>
    void GetCurrentPosition(uint32_t& play, uint32_t& write) const;

    /// <summary>Moves the play cursor (<c>SetCurrentPosition</c>), rounded down to a whole sample frame.</summary>
    void SetCurrentPosition(uint32_t offset);

    /// <summary>Sets the volume in hundredths of a decibel, -10000 (silent) to 0 (as recorded) (<c>SetVolume</c>).</summary>
    void SetVolume(int32_t hundredthsDb);

    /// <summary>The volume set last.</summary>
    int32_t GetVolume() const;

    /// <summary>
    /// Sets the pan in hundredths of a decibel (<c>SetPan</c>): negative attenuates the right channel by that much,
    /// positive the left; -10000 and 10000 are fully left and fully right.
    /// </summary>
    void SetPan(int32_t pan);

    /// <summary>The pan set last.</summary>
    int32_t GetPan() const;

    /// <summary>
    /// Sets the playback rate in Hz (<c>SetFrequency</c>), clamped to 100-100000; 0 goes back to the format's own
    /// rate.
    /// </summary>
    void SetFrequency(uint32_t hz);

    /// <summary>The playback rate in Hz (<c>GetFrequency</c>).</summary>
    uint32_t GetFrequency() const;

private:
    friend class MCAudio;
    friend struct MCAudioCore;

    /// <summary>The samples played: the shared ones while there are, else the buffer's own.</summary>
    std::span<const uint8_t> Bytes() const
    {
        return _Shared != nullptr ? std::span(*_Shared).first(_SharedSize) : std::span<const uint8_t>(_Data);
    }

    std::shared_ptr<MCAudioCore> _Core;
    MCSoundFormat _Format;
    std::vector<uint8_t> _Data;
    /// <summary>Samples played where they are (a decoded HD replacement), until a Lock copies them.</summary>
    std::shared_ptr<const std::vector<uint8_t>> _Shared;
    /// <summary>The shared samples' whole frames, in bytes.</summary>
    size_t _SharedSize = 0;

    // Guarded by the core's lock.
    bool _Playing = false;
    bool _Looping = false;
    /// <summary>Play cursor in sample frames, fractional because of resampling.</summary>
    double _Position = 0.0;
    int32_t _Volume = 0;
    int32_t _Pan = 0;
    uint32_t _Frequency = 0;
    bool _Registered = false;
};

/// <summary>
/// A queue of PCM that plays as it is fed: the sound of a movie, or anything decoded on the fly. What is queued plays
/// once, in order; when the queue runs dry the stream plays silence until more comes.
/// </summary>
/// <remarks>Thread-safe. Created by <see cref="MCAudio::CreateStream"/>; plays until the last reference is dropped.</remarks>
class MCAudioStream
{
    /// <summary>Only <see cref="MCAudio::CreateStream"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>An empty stream in <paramref name="format"/>, mixed by <paramref name="core"/>.</summary>
    MCAudioStream(Key, std::shared_ptr<MCAudioCore> core, const MCSoundFormat& format);

    ~MCAudioStream();
    MCAudioStream(const MCAudioStream&) = delete;
    MCAudioStream& operator=(const MCAudioStream&) = delete;

    /// <summary>The stream's format.</summary>
    const MCSoundFormat& Format() const { return _Format; }

    /// <summary>Appends PCM in the stream's format; a trailing partial sample frame is dropped.</summary>
    void Queue(std::span<const uint8_t> pcm);

    /// <summary>Sample frames queued and not yet played.</summary>
    uint64_t QueuedFrames() const;

    /// <summary>Sample frames played since the stream was made (or since <see cref="Clear"/>).</summary>
    uint64_t PlayedFrames() const;

    /// <summary>Drops what is queued and restarts the played count.</summary>
    void Clear();

    /// <summary>Holds the stream where it is, or lets it go on.</summary>
    void Pause(bool paused);

    /// <summary>Volume in hundredths of a decibel, as <see cref="MCSoundBuffer::SetVolume"/>.</summary>
    void SetVolume(int32_t hundredthsDb);

    /// <summary>Pan in hundredths of a decibel, as <see cref="MCSoundBuffer::SetPan"/>.</summary>
    void SetPan(int32_t pan);

private:
    friend class MCAudio;
    friend struct MCAudioCore;

    std::shared_ptr<MCAudioCore> _Core;
    MCSoundFormat _Format;

    // Guarded by the core's lock.
    /// <summary>Queued samples, converted to float, interleaved by the stream's channel count.</summary>
    std::deque<float> _Samples;
    /// <summary>
    /// Port: the samples at the queue's front the mixer has played, which Queue and Clear drop: the mixer's thread
    /// frees nothing under the lock.
    /// </summary>
    size_t _Head = 0;
    /// <summary>Fraction of a source frame the mixer has consumed past the queue's front.</summary>
    double _Phase = 0.0;
    uint64_t _Played = 0;
    bool _Paused = false;
    int32_t _Volume = 0;
    int32_t _Pan = 0;
};

/// <summary>
/// The port's replacement for DirectSound: one SDL playback device, and a software mixer that plays any number of
/// <see cref="MCSoundBuffer"/>s (the sound renderer's channels) and <see cref="MCAudioStream"/>s (movies) into it.
/// </summary>
/// <remarks>
/// <para>What the original used, from <c>game os\sound renderer</c>: one
/// DirectSound object with a 22050 Hz 16-bit stereo primary buffer, and per channel one secondary buffer, either a
/// whole WAV (static, played once or looping) or a ring buffer the streaming music is written into from a timer
/// thread with Lock/Unlock at the play cursor. Controls: volume, pan and frequency. No 3D, no effects.</para>
/// <para>The device runs at <see cref="OutputRate"/> in 32-bit float stereo; buffers of any rate are resampled
/// (linear interpolation). Everything the mixer reads is guarded by one lock, held for the length of a mix.
/// Without a device (<see cref="CreateSilent"/>) nothing plays until <see cref="Render"/> is called, which is what
/// the tests do.</para>
/// </remarks>
class MCAudio
{
    /// <summary>Only <see cref="Open"/> and <see cref="CreateSilent"/> make one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>The device's sample rate.</summary>
    static constexpr uint32_t OutputRate = 48000;

    /// <summary>Opens the default playback device (initialising SDL's audio subsystem if needed).</summary>
    /// <returns>The mixer, or why no device could be had (the game then runs with <see cref="CreateSilent"/>).</returns>
    static std::expected<std::unique_ptr<MCAudio>, std::string> Open();

    /// <summary>A mixer without a device: the same API, with <see cref="Render"/> as the only way out.</summary>
    static std::unique_ptr<MCAudio> CreateSilent();

    /// <summary>A mixer with no device yet.</summary>
    explicit MCAudio(Key);

    ~MCAudio();
    MCAudio(const MCAudio&) = delete;
    MCAudio& operator=(const MCAudio&) = delete;

    /// <summary>Whether a device is open.</summary>
    bool HasDevice() const { return _Device != nullptr; }

    /// <summary>Destroys the device stream (which stops its callback before it returns).</summary>
    using MCDeviceStream = std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)>;

    /// <summary>
    /// Creates a silent buffer of <paramref name="bytes"/> bytes (<c>CreateSoundBuffer</c>), rounded down to whole
    /// sample frames; fill it with <see cref="MCSoundBuffer::Lock"/> or <see cref="MCSoundBuffer::Write"/>.
    /// </summary>
    std::expected<std::shared_ptr<MCSoundBuffer>, std::string> CreateBuffer(const MCSoundFormat& format,
                                                                            uint32_t bytes);

    /// <summary>Creates a buffer holding a copy of <paramref name="pcm"/>, as the static channels load a WAV.</summary>
    std::expected<std::shared_ptr<MCSoundBuffer>, std::string> CreateBuffer(const MCSoundFormat& format,
                                                                            std::span<const uint8_t> pcm);

    /// <summary>
    /// Creates a buffer playing <paramref name="pcm"/> where it is, without a copy (the static channels' HD
    /// replacements, decoded once and kept); written to, it copies it first.
    /// </summary>
    std::expected<std::shared_ptr<MCSoundBuffer>, std::string> CreateBuffer(
        const MCSoundFormat& format, std::shared_ptr<const std::vector<uint8_t>> pcm);

    /// <summary>Creates a buffer from a whole RIFF WAVE file in memory.</summary>
    std::expected<std::shared_ptr<MCSoundBuffer>, std::string> CreateBufferFromWave(std::span<const uint8_t> wave);

    /// <summary>Creates a stream (it starts playing as soon as something is queued).</summary>
    std::expected<std::shared_ptr<MCAudioStream>, std::string> CreateStream(const MCSoundFormat& format);

    /// <summary>Scales everything, 0 (silent) to 1 (as mixed).</summary>
    void SetMasterVolume(float volume);

    /// <summary>Stops the device's output without stopping the buffers' clocks (the app lost focus, say).</summary>
    void PauseDevice(bool paused);

    /// <summary>
    /// Tells <paramref name="listener"/> (the <see cref="MCAudioDevice"/> that opened this mixer) of every buffer's
    /// <see cref="MCSoundBuffer::Play"/> and <see cref="MCSoundBuffer::Stop"/> from now on, while it lives; an empty
    /// one tells nobody.
    /// </summary>
    void SetListener(std::weak_ptr<MCAudioDevice> listener);

    /// <summary>
    /// Mixes <c>out.size() / 2</c> stereo frames into <paramref name="out"/> (overwritten) and advances every buffer
    /// and stream by that much. The device callback calls this; so can a test.
    /// </summary>
    void Render(std::span<float> out);

    /// <summary>
    /// Takes a RIFF WAVE apart as <c>SoundResource::GetWaveInfo</c> @ 0x00758790 does: the <c>fmt </c> chunk (PCM
    /// only) and the <c>data</c> chunk.
    /// </summary>
    static std::expected<MCWaveData, std::string> ParseWave(std::span<const uint8_t> file);

    /// <summary>A gain factor for a DirectSound volume in hundredths of a decibel (-10000 gives 0).</summary>
    static float GainFromHundredthsDb(int32_t hundredthsDb);

private:
    static void SDLCALL Feed(void* user, SDL_AudioStream* stream, int additional, int total);
    /// <summary>Puts a new buffer in the mix.</summary>
    std::shared_ptr<MCSoundBuffer> Register(std::shared_ptr<MCSoundBuffer> buffer);

    std::shared_ptr<MCAudioCore> _Core;
    MCDeviceStream _Device{nullptr, &SDL_DestroyAudioStream};
};
