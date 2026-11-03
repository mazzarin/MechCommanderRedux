#pragma once

#include "gameos/MCSoundResource.h"

class MCAudio;
class MCSoundChannel;

/// <summary>
/// The sound renderer (gameos's <c>gos_*</c> sound calls): its channels, every sound resource, and the mixer they
/// play through (DirectSound in the original). A game system of <see cref="MCGameContext"/> (SoundRenderer()), installed
/// by the sound system and uninstalled at shutdown.
/// </summary>
/// <remarks>Original source: <c>game os\sound renderer\sound renderer.cpp</c>, <c>sound channel.cpp</c>,
/// <c>sound resource.cpp</c>. Channels are numbered from 0, as the game's channel constants count them.</remarks>
class MCSoundRenderer
{
public:
    /// <summary>Makes <paramref name="numChannels"/> channels on <paramref name="mixer"/>.</summary>
    MCSoundRenderer(int32_t numChannels, std::unique_ptr<MCAudio> mixer);
    /// <summary>Deletes the channels, then every resource, then releases the mixer.</summary>
    ~MCSoundRenderer();

    MCSoundRenderer(const MCSoundRenderer&) = delete;
    MCSoundRenderer& operator=(const MCSoundRenderer&) = delete;

    /// <summary>
    /// Installs a renderer of <paramref name="numChannels"/> channels in the current context, on a mixer from its
    /// audio device. Without a playback device the mixer is silent (the original stopped: "DirectSound was unable to
    /// initialize").
    /// </summary>
    static MCSoundRenderer& Install(int32_t numChannels);
    /// <summary>Removes the current context's renderer, if it has one.</summary>
    static void Uninstall();

    /// <summary>
    /// Makes a memory resource of the caller's wave image (gos_CreateSoundResource), given its
    /// <see cref="MCHdAudio::SampleId"/> when the caller keeps it. The renderer keeps it until
    /// <see cref="DestroyResource"/>; the caller keeps a <c>weak_ptr</c>.
    /// </summary>
    std::shared_ptr<MCSoundResource> CreateResource(std::span<const uint8_t> image,
                                                    std::optional<uint64_t> hdId = std::nullopt);
    /// <summary>Makes a file or stream resource of <paramref name="fileName"/>.</summary>
    std::shared_ptr<MCSoundResource> CreateResource(MCSoundResourceType type, std::string_view fileName);
    /// <summary>Stops every channel playing the resource, then destroys it (gos_DestroySoundResource).</summary>
    void DestroyResource(const MCSoundResource& resource);
    /// <summary>How many resources there are.</summary>
    size_t ResourceCount() const { return _Resources.size(); }

    /// <summary>
    /// Plays <paramref name="resource"/> on the channel: a stream gets a stream buffer, anything else a buffer loaded
    /// with the whole wave. Nothing for a null resource.
    /// </summary>
    void Play(int32_t channel, std::shared_ptr<MCSoundResource> resource);
    /// <summary>Stops the channel.</summary>
    void Stop(int32_t channel);
    /// <summary>Whether the channel plays (gos_GetChannelStatus is 0).</summary>
    bool IsPlaying(int32_t channel) const;
    /// <summary>A channel's volume (0..1; the buffer gets -2000 - (int)(volume * -2000) hundredths of a dB, so 0 is
    /// -20 dB, not silence).</summary>
    float ChannelVolume(int32_t channel) const;
    /// <summary>Sets the channel's volume, and its buffer's if it has one.</summary>
    void SetChannelVolume(int32_t channel, float volume);
    /// <summary>Sets the channel's pan (-1..1; x 10000 on the buffer, OB-059).</summary>
    void SetChannelPanning(int32_t channel, float panning);
    /// <summary>Sets whether the channel's next play loops.</summary>
    void SetChannelLooping(int32_t channel, bool looping);
    /// <summary>The resource on the channel (null for none).</summary>
    std::shared_ptr<MCSoundResource> ChannelResource(int32_t channel) const;
    /// <summary>How many channels there are.</summary>
    int32_t ChannelCount() const { return static_cast<int32_t>(_Channels.size()); }
    /// <summary>
    /// Refills every streaming channel with what has played since its last fill, decoding without the lock (the
    /// original's per-channel stream timers; the port's mouse timer thread calls it every tick).
    /// <paramref name="checkEnds"/> also stops the non-looping streams whose duration has passed (the timer does every
    /// 550 ms, as the original's stream timer serviced them).
    /// </summary>
    void ServiceStreams(bool checkEnds = true);
    /// <summary>The mixer (Smacker's sound plays through it too).</summary>
    MCAudio& Mixer() { return *_Mixer; }

    /// <summary>The mixer, for those that refer to it while it lives (movie sound).</summary>
    std::weak_ptr<MCAudio> MixerReference() const { return _Mixer; }

private:
    /// <summary>Takes ownership of a new resource.</summary>
    std::shared_ptr<MCSoundResource> Adopt(std::shared_ptr<MCSoundResource> resource);

    /// <summary>The mixer.</summary>
    std::shared_ptr<MCAudio> _Mixer;
    /// <summary>Every live resource.</summary>
    std::vector<std::shared_ptr<MCSoundResource>> _Resources;
    /// <summary>The channels.</summary>
    std::vector<std::unique_ptr<MCSoundChannel>> _Channels;
    /// <summary>Guards the channels and resources against the stream-servicing thread (which decodes without it).</summary>
    mutable std::recursive_mutex _Lock;
};

/// <summary>The current context's sound renderer (null before the sound system installs it); the caller keeps it as its
/// pin.</summary>
std::shared_ptr<MCSoundRenderer> SoundRenderer();
