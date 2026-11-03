#pragma once

class MCAudio;
class MCSoundBuffer;
class MCSoundResource;

/// <summary>
/// One of the sound renderer's channels: a mixer buffer (a DirectSound buffer in the original) for a resource, either
/// loaded whole or refilled from a stream as it plays.
/// </summary>
/// <remarks>
/// Original source: <c>game os\sound renderer\sound channel.cpp</c>. Every channel the game opens asks for volume and
/// pan control (and no frequency control), so a new buffer always gets the channel's volume and pan; the property
/// bits, the frequency factor and the pause the game never used are gone.
/// </remarks>
class MCSoundChannel
{
public:
    /// <summary>Volume 1, pan 0; 4000 ms stream buffers. Buffers come from <paramref name="mixer"/>.</summary>
    explicit MCSoundChannel(MCAudio& mixer);
    /// <summary>Releases the buffer.</summary>
    ~MCSoundChannel();

    MCSoundChannel(const MCSoundChannel&) = delete;
    MCSoundChannel& operator=(const MCSoundChannel&) = delete;

    /// <summary>
    /// Makes a buffer of the resource's whole wave (an HD replacement's played where it is, the original's copied in);
    /// applies volume and pan.
    /// </summary>
    void CreateAndLoadBuffer(std::shared_ptr<MCSoundResource> newResource);
    /// <summary>Stops a playing buffer and rewinds it.</summary>
    void Stop();
    /// <summary>Restarts the buffer from the start, looping if set.</summary>
    void Play();
    /// <summary>
    /// Primes the stream buffer if needed (its first <c>STREAM_PRIME_MS</c>; the timer thread fills the rest, see
    /// <see cref="BeginFill"/>) and plays it looping; remembers the start time.
    /// </summary>
    void PlayStream();
    /// <summary>Makes a stream buffer of 4000 ms of the resource (at least its data size); applies volume and pan.
    /// </summary>
    void CreateStreamBuffer(std::shared_ptr<MCSoundResource> newResource);
    /// <summary>Stops a playing stream.</summary>
    void StopStream();

    /// <summary>
    /// A stream fill: the ring's bytes from <see cref="From"/> to write with the stream's next ones. Taken under the
    /// renderer's lock (<see cref="BeginFill"/>), written without it (<see cref="WriteFill"/>), ended under it again
    /// (<see cref="EndFill"/>): the game's thread may stop or restart the stream meanwhile.
    /// </summary>
    struct MCStreamFill
    {
        /// <summary>The ring written (kept while the fill writes, though the channel may drop it).</summary>
        std::shared_ptr<MCSoundBuffer> Buffer;
        /// <summary>The stream read (kept likewise).</summary>
        std::shared_ptr<MCSoundResource> Resource;
        /// <summary>Where the fill starts in the ring.</summary>
        uint32_t From = 0;
        /// <summary>The bytes to write.</summary>
        uint32_t Bytes = 0;
        /// <summary>The bytes written so far.</summary>
        uint32_t Written = 0;
        /// <summary>The ring's size.</summary>
        uint32_t RingSize = 0;
        /// <summary>The stream's play it belongs to (the channel's count of starts and stops when it was taken).</summary>
        uint32_t Generation = 0;
        /// <summary>Whether the stream loops.</summary>
        bool Looping = false;
    };

    /// <summary>
    /// Takes the next fill of the playing stream: what has played since the last write, which after a prime is the
    /// whole ring past it. A prime the play cursor already ran past (the fill came too late) is given up: the ring is
    /// filled on from a little ahead of the cursor, so the stream goes on after a short gap. Call with the renderer's
    /// lock held.
    /// </summary>
    /// <returns>The fill, or none: nothing has played, or a fill is still under way.</returns>
    std::optional<MCStreamFill> BeginFill();
    /// <summary>
    /// Writes <paramref name="fill"/>'s bytes of the stream, <c>STREAM_FILL_CHUNK_MS</c> at a time, each handed to the
    /// mixer as it is written. Call without the renderer's lock; it stops reading as soon as the stream is stopped or
    /// played again.
    /// </summary>
    void WriteFill(MCStreamFill& fill) const;
    /// <summary>Ends <paramref name="fill"/>: the next write goes after it, unless the stream was stopped or played again
    /// meanwhile. Call with the renderer's lock held.</summary>
    void EndFill(const MCStreamFill& fill);
    /// <summary>
    /// Stops a non-looping stream once its duration has passed by <paramref name="now"/> (ms). Call with the renderer's
    /// lock held.
    /// </summary>
    void StopStreamAtEnd(uint32_t now);
    /// <summary>Sets the volume (0..1), and the buffer's if there is one.</summary>
    void SetVolume(float volume);
    /// <summary>Sets the pan (-1..1), and the buffer's if there is one.</summary>
    void SetPanning(float panning);
    /// <summary>Whether the buffer plays (or a stream runs).</summary>
    bool IsPlaying() const;

    /// <summary>
    /// The buffer volume (hundredths of a dB) a channel volume (0..1) gives: the gain is the volume squared, and 0 is
    /// silence.
    /// </summary>
    static int32_t BufferVolume(float volume);

    /// <summary>0..1.</summary>
    float Volume = 1.0f;
    /// <summary>-1..1.</summary>
    float Panning = 0.0f;
    /// <summary>The resource played (the renderer owns it; destroying it ends the reference).</summary>
    std::weak_ptr<MCSoundResource> Resource;
    /// <summary>Whether it loops.</summary>
    bool Looping = false;
    /// <summary>Set while a stream plays (the timer thread clears it at the stream's end).</summary>
    std::atomic<bool> Streaming = false;

private:
    /// <summary>Drops the buffer (stopping it first when a fill under way still holds it).</summary>
    void DropBuffer();
    /// <summary>Makes a buffer of <paramref name="bytes"/> for the resource.</summary>
    /// <returns>Whether it could.</returns>
    bool CreateBuffer(uint32_t bytes);
    /// <summary>Applies the channel's volume and pan to its new buffer.</summary>
    void ApplyControls();
    /// <summary>Rewinds the resource and fills the stream buffer's first <c>STREAM_PRIME_MS</c>, once.</summary>
    void PrimeStream();
    /// <summary>A new start or stop of the stream: a fill under way for the one before stops and is dropped.</summary>
    void NewGeneration() { _Generation.fetch_add(1); }

    /// <summary>Where buffers come from.</summary>
    MCAudio& _Mixer;
    /// <summary>The buffer.</summary>
    std::shared_ptr<MCSoundBuffer> _Buffer;
    /// <summary>Set once the stream buffer is primed (PrimeStream).</summary>
    bool _StreamPrimed = false;
    /// <summary>Set from the prime to the first fill, which checks the prime hasn't run dry.</summary>
    bool _FirstFill = false;
    /// <summary>Set while a fill is under way (guarded by the renderer's lock).</summary>
    bool _Filling = false;
    /// <summary>Counts the stream's starts and stops, so a fill under way can tell it is no longer wanted.</summary>
    std::atomic<uint32_t> _Generation = 0;
    /// <summary>Where the next stream bytes go in the buffer.</summary>
    uint32_t _WritePos = 0;
    /// <summary>The stream buffer's size in bytes.</summary>
    uint32_t _BufferSize = 0;
    /// <summary>The resource's length in ms.</summary>
    uint32_t _DurationMs = 0;
    /// <summary>When the stream started (ms).</summary>
    uint32_t _StartTime = 0;
};
