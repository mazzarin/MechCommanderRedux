#include "stdafx.h"
#include "gameos/MCSoundChannel.h"
#include "gameos/MCSoundResource.h"
#include "lib/MCFatal.h"
#include "platform/MCAudio.h"

namespace
{
    /// <summary>The stream buffer's length in ms.</summary>
    constexpr uint32_t STREAM_BUFFER_MS = 4000;

    /// <summary>
    /// What a stream's play primes on the game's thread, in ms: the timer thread fills the rest of the ring at its next
    /// tick (every 50 ms), long before this much has played.
    /// </summary>
    constexpr uint32_t STREAM_PRIME_MS = 250;

    /// <summary>The ms of a stream the timer thread reads, and hands to the mixer, at a time.</summary>
    constexpr uint32_t STREAM_FILL_CHUNK_MS = 250;

    /// <summary>
    /// When a fill comes too late (the play cursor ran past the prime), how far ahead of the cursor in ms the stream
    /// goes on: the mixer plays on while the fill writes, so it isn't written where the mixer reads.
    /// </summary>
    constexpr uint32_t STREAM_RESUME_LEAD_MS = 50;

    /// <summary>
    /// Hundredths of a decibel per decade of channel volume: 4000 makes the gain the volume squared (2000 per decade
    /// of gain), the usual even-sounding fader taper.
    /// </summary>
    constexpr float VOLUME_TAPER_HUNDREDTHS_DB = 4000.0f;

    /// <summary>The DirectSound pan a channel pan gives: ftol(pan) * 10000, so only -1, 0 and 1 are heard (OB-059).
    /// </summary>
    int32_t BufferPan(float panning)
    {
        return static_cast<int32_t>(panning) * 10000;
    }

    /// <summary>A resource's format as the port's mixer takes it.</summary>
    MCSoundFormat SoundFormat(const tWAVEFORMATEX& format)
    {
        MCSoundFormat result;
        result.Rate = format.nSamplesPerSec;
        result.Channels = format.nChannels;
        result.Bits = format.wBitsPerSample;
        return result;
    }

    /// <summary><paramref name="ms"/> of <paramref name="format"/> in bytes, in whole sample frames.</summary>
    uint32_t StreamBytes(const tWAVEFORMATEX& format, uint32_t ms)
    {
        const uint32_t frame = std::max<uint32_t>(format.nBlockAlign, 1);
        return static_cast<uint32_t>(uint64_t{format.nAvgBytesPerSec} * ms / 1000) / frame * frame;
    }

    /// <summary>
    /// Writes the stream's next <paramref name="bytes"/> into <paramref name="buffer"/> at <paramref name="at"/>,
    /// wrapping. Fatal when it can't.
    /// </summary>
    /// <returns>The bytes written.</returns>
    uint32_t WriteWave(MCSoundBuffer& buffer, MCSoundResource& resource, uint32_t at, uint32_t bytes, bool loop)
    {
        MCSoundBuffer::MCLockedRegion region = buffer.Lock(at, bytes);
        uint32_t read2 = 0;
        const uint32_t read1 = resource.Read(region.First, loop);

        if (read1 != region.First.size())
        {
            Fatal(-1, "Cannot read wave data!");
        }

        if (!region.Second.empty())
        {
            read2 = resource.Read(region.Second, loop);

            if (read2 != region.Second.size())
            {
                Fatal(-1, "Cannot read wave data!");
            }
        }

        buffer.Unlock(region);
        return read1 + read2;
    }
}

MCSoundChannel::MCSoundChannel(MCAudio& mixer) : _Mixer(mixer)
{
}

int32_t MCSoundChannel::BufferVolume(float volume)
{
    // OB-176 fixed: the original's -2000 - ftol(volume * -2000) never went below -20 dB.
    if (volume <= 0.0f)
    {
        return MCSoundBuffer::VolumeMin;
    }

    if (volume >= 1.0f)
    {
        return MCSoundBuffer::VolumeMax;
    }

    const auto hundredthsDb = std::lround(VOLUME_TAPER_HUNDREDTHS_DB * std::log10(volume));
    return static_cast<int32_t>(std::max<long>(hundredthsDb, MCSoundBuffer::VolumeMin));
}

MCSoundChannel::~MCSoundChannel() = default;

void MCSoundChannel::DropBuffer()
{
    // A fill under way holds the old buffer till it ends: it mustn't be heard meanwhile.
    if (_Filling && _Buffer != nullptr && _Buffer->IsPlaying())
    {
        _Buffer->Stop();
    }

    _Buffer.reset();
}

bool MCSoundChannel::CreateBuffer(uint32_t bytes)
{
    DropBuffer();
    auto buffer = _Mixer.CreateBuffer(SoundFormat(Resource.lock()->Format), bytes);

    if (!buffer)
    {
        return false;
    }

    _Buffer = std::move(*buffer);
    return true;
}

void MCSoundChannel::ApplyControls()
{
    _Buffer->SetVolume(BufferVolume(Volume));
    _Buffer->SetPan(BufferPan(Panning));
}

void MCSoundChannel::CreateAndLoadBuffer(std::shared_ptr<MCSoundResource> newResource)
{
    NewGeneration();
    Resource = newResource;
    DropBuffer();

    // The buffer plays an HD replacement's decoded samples where they are; the original's wave is copied in.
    const MCSoundFormat format = SoundFormat(newResource->Format);
    auto buffer = newResource->SharedWaveData() != nullptr ? _Mixer.CreateBuffer(format, newResource->SharedWaveData())
                                                           : _Mixer.CreateBuffer(format, newResource->WaveData());

    if (!buffer)
    {
        Fatal(-1, "CreateSoundBuffer failed!");
    }

    _Buffer = std::move(*buffer);
    ApplyControls();
}

void MCSoundChannel::Stop()
{
    if (_Buffer->IsPlaying())
    {
        _Buffer->Stop();
        _Buffer->SetCurrentPosition(0);
    }
}

void MCSoundChannel::Play()
{
    if (_Buffer->IsPlaying())
    {
        _Buffer->Stop();
    }

    _Buffer->SetCurrentPosition(0);
    _Buffer->Play(Looping);
}

void MCSoundChannel::PlayStream()
{
    if (_Buffer == nullptr)
    {
        return;
    }

    if (Streaming)
    {
        StopStream();
    }

    NewGeneration();

    if (!_StreamPrimed)
    {
        PrimeStream();
    }

    // DirectSound's Play could fail (Fatal "Error: playstream failed!"); the port's cannot.
    _Buffer->Play(true);
    _StartTime = MCPort::Milliseconds();
    Streaming = true;
    _StreamPrimed = false;
}

void MCSoundChannel::CreateStreamBuffer(std::shared_ptr<MCSoundResource> newResource)
{
    NewGeneration();
    Resource = newResource;
    _BufferSize = std::max(newResource->Format.nAvgBytesPerSec * STREAM_BUFFER_MS / 1000, newResource->WaveSize);
    _DurationMs = newResource->DurationMs;

    // The original ignored a failure here; the stream then never plays.
    if (CreateBuffer(_BufferSize))
    {
        ApplyControls();
    }
}

void MCSoundChannel::PrimeStream()
{
    if (_StreamPrimed)
    {
        return;
    }

    // The original filled the whole 4 s ring here, on the game's thread; the port primes a quarter second of it (a
    // whole ring when it is that short) and the timer thread fills the rest at its next tick.
    const std::shared_ptr<MCSoundResource> resource = Resource.lock();
    const std::unique_lock stream = resource->LockStream();
    _WritePos = 0;
    resource->Rewind();
    _Buffer->SetCurrentPosition(0);
    _StreamPrimed = true;
    _FirstFill = false;

    // Issue #25: a stream without a byte rate has an empty ring (the original divided by its size).
    if (_BufferSize == 0)
    {
        return;
    }

    const uint32_t prime = std::min(_BufferSize, std::max(StreamBytes(resource->Format, STREAM_PRIME_MS), 1u));
    _WritePos = WriteWave(*_Buffer, *resource, 0, prime, Looping) % _BufferSize;
    _FirstFill = prime < _BufferSize;
}

void MCSoundChannel::StopStream()
{
    if (!Streaming)
    {
        return;
    }

    NewGeneration();
    _Buffer->Stop();
    Streaming = false;
}

std::optional<MCSoundChannel::MCStreamFill> MCSoundChannel::BeginFill()
{
    const std::shared_ptr<MCSoundResource> resource = Resource.lock();

    if (_Filling || !Streaming || resource == nullptr || _Buffer == nullptr || _BufferSize == 0)
    {
        return std::nullopt;
    }

    uint32_t play = 0;
    uint32_t write = 0;
    _Buffer->GetCurrentPosition(play, write);
    // What has played since the last write: after a prime, the whole ring past it.
    MCStreamFill fill{.Buffer = _Buffer,
                      .Resource = resource,
                      .From = _WritePos,
                      .Bytes = _WritePos <= play ? play - _WritePos : _BufferSize - _WritePos + play,
                      .RingSize = _BufferSize,
                      .Generation = _Generation.load(),
                      .Looping = Looping};

    // The prime ran dry before this first fill: what the ring holds past it (silence) is playing. The stream goes on
    // from a little ahead of the play cursor, round to just behind it.
    if (_FirstFill && play >= _WritePos)
    {
        const uint32_t lead = std::min(StreamBytes(resource->Format, STREAM_RESUME_LEAD_MS), _BufferSize);
        fill.From = (play + lead) % _BufferSize;
        fill.Bytes = _BufferSize - lead;
    }

    _FirstFill = false;

    if (fill.Bytes == 0)
    {
        return std::nullopt;
    }

    _Filling = true;
    return fill;
}

void MCSoundChannel::WriteFill(MCStreamFill& fill) const
{
    const uint32_t chunk = std::max(StreamBytes(fill.Resource->Format, STREAM_FILL_CHUNK_MS), 1u);

    while (fill.Written < fill.Bytes)
    {
        // Each chunk is read under the stream's lock, and only while the stream is still the one this fill is for: a
        // play or stop on the game's thread meanwhile starts a new generation (and a new play rewinds the stream).
        const std::unique_lock stream = fill.Resource->LockStream();

        if (_Generation.load() != fill.Generation)
        {
            return;
        }

        fill.Written += WriteWave(*fill.Buffer, *fill.Resource, (fill.From + fill.Written) % fill.RingSize,
                                  std::min(chunk, fill.Bytes - fill.Written), fill.Looping);
    }
}

void MCSoundChannel::EndFill(const MCStreamFill& fill)
{
    _Filling = false;

    if (_Generation.load() == fill.Generation && fill.Written == fill.Bytes)
    {
        _WritePos = (fill.From + fill.Written) % fill.RingSize;
    }
}

void MCSoundChannel::StopStreamAtEnd(uint32_t now)
{
    if (Streaming && _DurationMs <= now - _StartTime && !Looping)
    {
        StopStream();
    }
}

void MCSoundChannel::SetVolume(float volume)
{
    if (_Buffer != nullptr)
    {
        _Buffer->SetVolume(BufferVolume(volume));
    }

    Volume = volume;
}

void MCSoundChannel::SetPanning(float panning)
{
    if (_Buffer != nullptr)
    {
        _Buffer->SetPan(BufferPan(panning));
    }

    Panning = panning;
}

bool MCSoundChannel::IsPlaying() const
{
    if (Resource.expired())
    {
        return false;
    }

    return Streaming || (_Buffer != nullptr && _Buffer->IsPlaying());
}
