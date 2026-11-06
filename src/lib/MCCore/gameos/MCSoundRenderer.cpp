#include "stdafx.h"
#include "gameos/MCSoundRenderer.h"
#include "gameos/MCSoundChannel.h"
#include "main/MCGameContext.h"
#include "platform/MCAudio.h"
#include "platform/MCSmacker.h"

MCSoundRenderer::MCSoundRenderer(int32_t numChannels, std::unique_ptr<MCAudio> mixer) : _Mixer(std::move(mixer))
{
    // The primary buffer (22050 Hz 16-bit stereo, played looping to keep the device's format) has no counterpart:
    // the port's mixer sets its own output format.
    _Channels.reserve(static_cast<size_t>(numChannels));

    for (int32_t i = 0; i < numChannels; i++)
    {
        _Channels.push_back(std::make_unique<MCSoundChannel>(*_Mixer));
    }
}

MCSoundRenderer::~MCSoundRenderer()
{
    std::scoped_lock lock(_Lock);
    _Channels.clear();
    _Resources.clear();
}

MCSoundRenderer& MCSoundRenderer::Install(int32_t numChannels)
{
    MCGameContext& context = MCGameContext::Current();
    // Port fix: without a playback device the game runs silent instead of stopping; see MCSdlAudioDevice.
    auto renderer = std::make_shared<MCSoundRenderer>(numChannels, context.Audio().OpenMixer());
    MCSoundRenderer& installed = *renderer;
    context.SetSoundRenderer(std::move(renderer));
    return installed;
}

void MCSoundRenderer::Uninstall()
{
    MCGameContext::Current().SetSoundRenderer(nullptr);
}

std::shared_ptr<MCSoundResource> MCSoundRenderer::Adopt(std::shared_ptr<MCSoundResource> resource)
{
    std::scoped_lock lock(_Lock);
    return _Resources.emplace_back(std::move(resource));
}

std::shared_ptr<MCSoundResource> MCSoundRenderer::CreateResource(std::span<const uint8_t> image,
                                                                 std::optional<uint64_t> hdId)
{
    return Adopt(std::make_shared<MCSoundResource>(image, hdId));
}

std::shared_ptr<MCSoundResource> MCSoundRenderer::CreateResource(MCSoundResourceType type, std::string_view fileName)
{
    return Adopt(std::make_shared<MCSoundResource>(type, fileName));
}

void MCSoundRenderer::DestroyResource(const MCSoundResource& resource)
{
    std::scoped_lock lock(_Lock);

    for (int32_t i = 0; i < ChannelCount(); i++)
    {
        if (_Channels[i]->Resource.lock().get() == &resource)
        {
            Stop(i);
            _Channels[i]->Resource.reset();
        }
    }

    auto found = std::ranges::find(_Resources, &resource, &std::shared_ptr<MCSoundResource>::get);

    if (found != _Resources.end())
    {
        _Resources.erase(found);
    }
}

void MCSoundRenderer::Play(int32_t channel, std::shared_ptr<MCSoundResource> resource)
{
    if (resource == nullptr)
    {
        return;
    }

    std::scoped_lock lock(_Lock);
    MCSoundChannel& soundChannel = *_Channels[channel];

    if (resource->Type != MCSoundResourceType::Stream)
    {
        soundChannel.CreateAndLoadBuffer(resource);
        soundChannel.Play();
        return;
    }

    if (soundChannel.Streaming)
    {
        soundChannel.StopStream();
    }

    soundChannel.CreateStreamBuffer(resource);
    resource->Rewind();
    soundChannel.PlayStream();
}

void MCSoundRenderer::Stop(int32_t channel)
{
    std::scoped_lock lock(_Lock);
    MCSoundChannel& soundChannel = *_Channels[channel];
    const std::shared_ptr<MCSoundResource> resource = soundChannel.Resource.lock();

    if (resource == nullptr)
    {
        return;
    }

    if (resource->Type != MCSoundResourceType::Stream)
    {
        soundChannel.Stop();
        return;
    }

    soundChannel.StopStream();
}

bool MCSoundRenderer::IsPlaying(int32_t channel) const
{
    return _Channels[channel]->IsPlaying();
}

float MCSoundRenderer::ChannelVolume(int32_t channel) const
{
    return _Channels[channel]->Volume;
}

void MCSoundRenderer::SetChannelVolume(int32_t channel, float volume)
{
    _Channels[channel]->SetVolume(volume);
}

void MCSoundRenderer::SetChannelPanning(int32_t channel, float panning)
{
    _Channels[channel]->SetPanning(panning);
}

void MCSoundRenderer::SetChannelLooping(int32_t channel, bool looping)
{
    _Channels[channel]->Looping = looping;
}

std::shared_ptr<MCSoundResource> MCSoundRenderer::ChannelResource(int32_t channel) const
{
    return _Channels[channel]->Resource.lock();
}

void MCSoundRenderer::ServiceStreams(bool checkEnds)
{
    // The fills are taken and ended under the lock, and written without it: decoding a stream's next bytes holds up no
    // sound start on the game's thread.
    std::vector<std::pair<std::reference_wrapper<MCSoundChannel>, MCSoundChannel::MCStreamFill>> fills;
    std::optional<uint32_t> now;

    {
        std::scoped_lock lock(_Lock);

        for (const std::unique_ptr<MCSoundChannel>& channel : _Channels)
        {
            if (const std::shared_ptr<MCSoundResource> resource = channel->Resource.lock();
                resource != nullptr && resource->Type == MCSoundResourceType::Stream && channel->Streaming)
            {
                if (std::optional<MCSoundChannel::MCStreamFill> fill = channel->BeginFill())
                {
                    fills.emplace_back(*channel, std::move(*fill));
                }
            }
        }
    }

    for (auto& [channel, fill] : fills)
    {
        channel.get().WriteFill(fill);
    }

    std::scoped_lock lock(_Lock);

    for (const auto& [channel, fill] : fills)
    {
        channel.get().EndFill(fill);
    }

    if (!checkEnds)
    {
        return;
    }

    for (const std::unique_ptr<MCSoundChannel>& channel : _Channels)
    {
        if (const std::shared_ptr<MCSoundResource> resource = channel->Resource.lock();
            resource != nullptr && resource->Type == MCSoundResourceType::Stream && channel->Streaming)
        {
            // The clock is read only for a stream playing, as the original's stream timer read it.
            now = now.has_value() ? now : MCPort::Milliseconds();
            channel->StopStreamAtEnd(*now);
        }
    }
}

std::shared_ptr<MCSoundRenderer> SoundRenderer()
{
    return MCGameContext::Current().SoundRenderer();
}
