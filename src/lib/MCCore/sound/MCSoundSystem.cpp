#include "stdafx.h"
#include "sound/MCSoundSystem.h"
#include "ai/MCMoveGeometry.h"
#include "camera/MCCamera.h"
#include "gameos/MCSoundChannel.h"
#include "gameos/MCSoundRenderer.h"
#include "gui/MCGuiSmackerWindow.h"
#include "lib/MCDice.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCFrameOfRef.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCGameContext.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "object/MCBigGameObject.h"
#include "object/MCMechWarrior.h"
#include "platform/MCFlac.h"
#include "platform/MCSmacker.h"
#include "sound/MCHdAudio.h"
#include "sprite/MCWeather.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"

int32_t UseSound = 1;
int32_t UseMusic = 1;
int32_t MusicVolume = 64;
int32_t RadioVolume = 64;
int32_t SfxVolume = 64;
int32_t InterfaceVolume = 64;
int32_t InCombat = 0;

namespace
{
    /// <summary>The fire loop's sound bite.</summary>
    constexpr int32_t FIRE_BITE = 0x16;
    /// <summary>Betty's samples.</summary>
    constexpr uint32_t BETTY_SAMPLES = 0x26;
    /// <summary>The ambient music.</summary>
    constexpr int32_t AMBIENT_MUSIC = 0x15;
    /// <summary>The second planet's music follows the first's at this offset.</summary>
    constexpr int32_t SECOND_PLANET_MUSIC = 0x19;
    /// <summary>What PlayDigitalMusic returns when it can't start the music now.</summary>
    constexpr int32_t MUSIC_BUSY = -0x5445fff2;
    /// <summary>The seconds the rain bed takes to ease from none to a downpour's, and back.</summary>
    constexpr float RainFade = 2.0f;
    /// <summary>
    /// The seconds of a pack sound (the rain loop, thunder) kept queued ahead of what plays, so a slow frame doesn't run
    /// it dry.
    /// </summary>
    constexpr uint32_t PackAheadSeconds = 1;
    /// <summary>The sample frames read from a pack sound at a time.</summary>
    constexpr size_t PackChunkFrames = 4096;
    /// <summary>The nearest clap plays this much faster than the thunder as made, the farthest this much (deeper, longer).</summary>
    constexpr float ThunderNearSpeed = 1.08f;
    constexpr float ThunderFarSpeed = 0.8f;
    /// <summary>How loud the farthest clap plays, of the nearest (a channel volume).</summary>
    constexpr float ThunderFarLevel = 0.45f;
    /// <summary>The most a clap is panned to a side, in hundredths of a decibel.</summary>
    constexpr float ThunderPanMost = 600.0f;
    /// <summary>The seconds a clap still rolling takes to fade out under a newer one.</summary>
    constexpr float ThunderFadeOut = 1.5f;
    /// <summary>The most the pack's thunder may decode to as a mission loads (its 23 s are 4.5 MB); past it, it streams.
    /// </summary>
    constexpr uint64_t ThunderMostBytes = 32u * 1024u * 1024u;

    /// <summary>A stream of sound in <paramref name="format"/> played at <paramref name="speed"/> times its rate.</summary>
    std::expected<std::shared_ptr<MCAudioStream>, std::string> StreamAtSpeed(MCAudio& mixer, MCSoundFormat format,
                                                                             float speed)
    {
        format.Rate = static_cast<uint32_t>(std::lround(static_cast<float>(format.Rate) * speed));
        return mixer.CreateStream(format);
    }

    /// <summary>
    /// A stream playing <paramref name="file"/> (the HD audio pack's sound at <paramref name="path"/>, opened; null: the
    /// pack lacks it or it isn't FLAC) through <paramref name="mixer"/> at <paramref name="speed"/> times its rate
    /// (faster: higher); null, the file closed, when there is none or it won't play (logged, as
    /// <paramref name="what"/>).
    /// </summary>
    std::shared_ptr<MCAudioStream> StreamPackSound(MCAudio& mixer, const std::optional<std::filesystem::path>& path,
                                                   std::string_view what, float speed, std::unique_ptr<MCFlac>& file)
    {
        std::expected<std::shared_ptr<MCAudioStream>, std::string> stream =
            std::unexpected(file != nullptr ? "it holds no sound" : "not a FLAC file");

        if (file != nullptr && file->Frames() > 0)
        {
            stream = StreamAtSpeed(mixer, file->Format(), speed);
        }

        if (!stream.has_value())
        {
            if (path.has_value())
            {
                SDL_Log("MCSoundSystem: the %.*s %s won't play: %s", static_cast<int>(what.size()), what.data(),
                        path->string().c_str(), stream.error().c_str());
            }

            file.reset();
            return nullptr;
        }

        return std::move(*stream);
    }

    /// <summary>A byte volume (0..127) as a channel volume.</summary>
    float ChannelVolume(uint32_t volume)
    {
        return static_cast<float>(volume) * (1.0f / 128.0f);
    }

    /// <summary>
    /// Reads a required setting of the .snd file. A missing one is logged (the original's Assert) and leaves the
    /// value zero.
    /// </summary>
    template <MCFitValue T> void ReadSetting(MCFitIniFile& file, std::string_view name, T& value, std::string_view what)
    {
        const MCFitResult<T> result = file.Read<T>(name);

        if (result.has_value())
        {
            value = *result;
            return;
        }

        if (result.error() == MCFitError::VariableNotFound)
        {
            value = T{};
        }

        Assert(false, static_cast<uint32_t>(std::to_underlying(result.error())), what);
    }

    /// <summary>Seeks a block of the .snd file; a missing one is logged.</summary>
    void SeekSoundBlock(MCFitIniFile& file, std::string_view block, std::string_view what)
    {
        const int32_t result = file.SeekBlock(block);
        Assert(result == 0, static_cast<uint32_t>(result), what);
    }
}

bool IsWaveImage(std::span<const uint8_t> data)
{
    // Port: a buffer too short for the header isn't one (the original read past it).
    if (data.size() < 12)
    {
        return false;
    }

    uint32_t riff;
    uint32_t wave;
    std::memcpy(&riff, data.data(), 4);
    std::memcpy(&wave, data.subspan(8).data(), 4);
    return riff == 0x46464952 && wave == 0x45564157;
}

float SoundBiteGain(float volume)
{
    // The original's channel volume was linear in decibels (OB-176): a bite's volume v was 20 * (v - 1) dB. Under
    // the port's taper (40 * log10 of the channel volume) that is a factor of 10^((v - 1) / 2).
    return std::pow(10.0f, (std::clamp(volume, 0.0f, 1.0f) - 1.0f) * 0.5f);
}

int32_t SoundPanPosition(float dx, float dy)
{
    MCVector3D axisX = UnitX;
    MCVector3D axisY = UnitY;
    const float s = static_cast<float>(std::sin(0.7853981633974483));
    const float c = static_cast<float>(std::cos(0.7853981633974483));
    const MCVector3D originalX = axisX;
    const MCVector3D rotated = axisY * s;
    axisX = axisX * c + rotated;
    axisY = axisY * c - originalX * s;
    MCVector3D up;
    up.X = -axisY.X;
    up.Y = -axisY.Y;
    up.Z = -axisY.Z;
    const float upX = up.X;
    const float upY = up.Y;
    MCVector3D toSound;
    toSound.X = dx;
    toSound.Y = dy;
    toSound.Z = 0.0f;
    up.Normalize();
    toSound.Normalize();
    double angle = AcosMatherr(up | toSound) * 0x1.ca5dc1a6402aap+5;

    if (upX * dy - upY * dx <= 0.0f)
    {
        angle = -angle;
    }

    if (angle < 0.0)
    {
        if (angle < -90.0)
        {
            angle = angle + 180.0;
        }
        else
        {
            angle = std::fabs(angle);
        }

        const int16_t steps = static_cast<int16_t>(std::floor(angle * (1.0 / 90.0) * 64.0f));
        return 0x40 - steps;
    }

    if (angle > 90.0)
    {
        angle = 180.0 - angle;
    }

    const int16_t steps = static_cast<int16_t>(std::floor(angle * (1.0 / 90.0) * 64.0f));
    return steps + 0x40;
}

MCSoundSystem::MCSoundSystem() : _Renderer(MCSoundRenderer::Install(NUM_SOUND_CHANNELS))
{
    SmackSoundUseDirectSound(_Renderer.MixerReference());
    _SoundOn = true;
}

MCSoundSystem::MCSoundSystem(std::string_view soundFileName) : MCSoundSystem()
{
    if (UseSound != 0)
    {
        Load(soundFileName);
    }
}

MCSoundSystem::~MCSoundSystem()
{
    if (UseSound == 0)
    {
        return;
    }

    PurgeSoundSystem();
    _SoundOn = false;
}

void MCSoundSystem::Load(std::string_view soundFileName)
{
    MCFitIniFile soundFile;
    int32_t result = soundFile.Open(GamePath(SoundPath, soundFileName, ".snd"));
    Assert(result == 0, static_cast<uint32_t>(result), " Error opening .SND file ");
    SeekSoundBlock(soundFile, "SoundSetup", " Error seeking block in .SND file ");
    // The rates, the DirectSound flag and the heap size are still required, then ignored: the mixer picks its
    // format, and the port has no sound heap.
    uint32_t ignored = 0;
    ReadSetting(soundFile, "sampleRate", ignored, " Couldn't find sampleRate in .SND file ");
    ReadSetting(soundFile, "bitDepth", ignored, " Couldn't find bitDepth in .SND file ");
    ReadSetting(soundFile, "channels", ignored, " Couldn't find channels in .SND file ");
    ReadSetting(soundFile, "DirectSound", ignored, " Couldn't find DirectSound in .SND file ");
    ReadSetting(soundFile, "soundHeapSize", ignored, " Couldn't find soundHeapSize in .SND file ");
    MusicLevel = static_cast<uint8_t>(MusicVolume);
    RadioLevel = static_cast<uint8_t>(RadioVolume);
    DigitalMasterVolume = static_cast<uint8_t>(SfxVolume);
    InterfaceLevel = static_cast<uint8_t>(InterfaceVolume);
    ReadSetting(soundFile, "MaxSoundDistance", MaxSoundDistance, " Couldn't find maxSoundDistance in .SND file ");
    ReadSetting(soundFile, "wcSampleRate", ignored, " Couldn't find a variable in .SND file ");
    ReadSetting(soundFile, "wcBitDepth", ignored, " Couldn't find a variable in .SND file ");
    ReadSetting(soundFile, "wcChannels", ignored, " Couldn't find a variable in .SND file ");

    // The original turned sound off here when waveOutGetNumDevs found no device ("No Digital Sound Hardware
    // Installed"). The port's renderer plays silent without one.
    _SoundDataFile = std::make_unique<MCPacketFile>();
    result = _SoundDataFile->Open(GamePath(SoundPath, soundFileName, ".pak"));
    Assert(result == 0, static_cast<uint32_t>(result), " Sound file initialization failed ");
    _BettyDataFile = std::make_unique<MCPacketFile>();
    result = _BettyDataFile->Open(GamePath(SoundPath, "Betty", ".pak"));
    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open bettyDataFile ");
    SeekSoundBlock(soundFile, "SoundBites", " Couldn't find a variable in betty file ");
    uint32_t numBites = 0;
    ReadSetting(soundFile, "numBites", numBites, " Couldn't find a variable in betty file ");
    Sounds.clear();
    Sounds.resize(numBites);

    for (int32_t i = 0; i < static_cast<int32_t>(numBites); i++)
    {
        SeekSoundBlock(soundFile, std::format("SoundBite{}", i), " Couldn't find a variable in betty file ");
        MCSoundBite& bite = Sounds[i];
        ReadSetting(soundFile, "priority", bite.Priority, " Couldn't find a variable in betty file ");
        ReadSetting(soundFile, "cache", bite.Cache, " Couldn't find a variable in betty file ");
        ReadSetting(soundFile, "soundId", bite.SoundId, " Couldn't find a variable in betty file ");

        if (soundFile.Read<uint32_t>("preload").value_or(0) != 0)
        {
            PreloadSoundBite(i);
        }

        ReadSetting(soundFile, "volume", bite.Volume, " Couldn't find a variable in betty file ");
    }

    SeekSoundBlock(soundFile, "DigitalMusic", " Couldn't find a music block in sound file ");
    int32_t musicCount = 0;
    ReadSetting(soundFile, "NumDMS", musicCount, " Couldn't find a variable in sound file ");
    ReadSetting(soundFile, "StreamFadeDownTime", StreamFadeDownTime, " Couldn't find a variable in sound file ");
    ReadSetting(soundFile, "StreamBitDepth", ignored, " Couldn't find a variable in sound file ");
    ReadSetting(soundFile, "StreamChannels", ignored, " Couldn't find a variable in sound file ");
    ReadSetting(soundFile, "DigitalStreamBufferSize", ignored, " Couldn't find a variable in sound file ");
    DigitalMusicIds.assign(static_cast<size_t>(std::max(musicCount, 0)), std::string());

    for (int32_t i = 0; i < musicCount; i++)
    {
        ReadSetting(soundFile, std::format("DMS{}", i), DigitalMusicIds[i], " Couldn't find a variable in sound file ");
        // The loop flags are read (and required), but each caller says whether its music loops.
        bool loop = false;
        ReadSetting(soundFile, std::format("DMSLoop{}", i), loop, " Couldn't find a variable in sound file ");
    }

    soundFile.Close();
    WholeMsgDone = true;
    PrepareHdSounds();
}

void MCSoundSystem::PrepareHdSounds()
{
    if (MCHdAudio::Prepared())
    {
        return;
    }

    std::vector<std::vector<uint8_t>> waves;
    const auto readAll = [&waves](MCPacketFile& file)
    {
        for (int32_t packet = 0; file.IsOpen() && packet < file.GetNumPackets(); packet++)
        {
            if (file.SeekPacket(packet) != 0 || file.GetPacketSize() <= 0)
            {
                continue;
            }

            std::vector<uint8_t> wave(static_cast<size_t>(file.GetPacketSize()));

            if (file.ReadPacket(packet, wave) == static_cast<int32_t>(wave.size()))
            {
                waves.push_back(std::move(wave));
            }
        }
    };

    if (_SoundDataFile != nullptr)
    {
        readAll(*_SoundDataFile);
    }

    if (_BettyDataFile != nullptr)
    {
        readAll(*_BettyDataFile);
    }

    if (MCPacketFile noise; noise.Open(GamePath(CDsoundPath, "noise", ".pak")) == 0)
    {
        readAll(noise);
    }

    MCHdAudio::Prepare(std::move(waves), DigitalMusicIds);
}

bool MCSoundSystem::PreloadSoundBite(int32_t biteId)
{
    MCPacketFile& file = *_SoundDataFile;

    // Issue #25: a bite the sound file doesn't list ("numBites") has no slot to load into.
    if (biteId < 0 || static_cast<size_t>(biteId) >= Sounds.size() || file.SeekPacket(biteId) != 0)
    {
        return false;
    }

    const uint32_t size = static_cast<uint32_t>(file.GetPacketSize());

    if (size == 0)
    {
        return false;
    }

    MCSoundBite& bite = Sounds[biteId];

    if (bite.BiteData.empty())
    {
        bite.BiteData.resize(size);
    }

    file.ReadPacket(biteId, std::span(bite.BiteData).first(std::min<size_t>(size, bite.BiteData.size())));
    _BiteHdIds.resize(Sounds.size());
    _BiteHdIds[biteId] = {MCHdAudio::SampleId(bite.BiteData), bite.BiteData.data(), bite.BiteData.size()};
    return true;
}

std::optional<uint64_t> MCSoundSystem::BiteHdId(int32_t biteId) const
{
    if (biteId < 0 || static_cast<size_t>(biteId) >= _BiteHdIds.size() || static_cast<size_t>(biteId) >= Sounds.size())
    {
        return std::nullopt;
    }

    const MCBiteHdId& known = _BiteHdIds[biteId];
    const std::vector<uint8_t>& data = Sounds[biteId].BiteData;
    return known.Data == data.data() && known.Size == data.size() ? known.Id : std::nullopt;
}

std::optional<uint64_t> MCSoundSystem::BettyHdId()
{
    if (_BettyLoaded < 0)
    {
        return std::nullopt;
    }

    const auto [found, added] = _BettyHdIds.try_emplace(_BettyLoaded);

    if (added)
    {
        found->second = MCHdAudio::SampleId(_BettySoundBite);
    }

    return found->second;
}

std::span<const uint8_t> MCSoundSystem::LoadBettySample(int32_t bettyId)
{
    MCPacketFile& file = *_BettyDataFile;

    if (file.SeekPacket(bettyId) != 0)
    {
        return {};
    }

    const uint32_t size = static_cast<uint32_t>(file.GetPacketSize());

    if (size != 0)
    {
        _BettySoundBite.assign(size, 0);
    }

    LastBettyId = bettyId;
    const int32_t read = file.ReadPacket(bettyId, std::span(_BettySoundBite).first(size));

    // Port: which sample the bytes are, for its HD audio identity; none known when the packet didn't read whole.
    if (size != 0)
    {
        _BettyLoaded = read == static_cast<int32_t>(size) ? bettyId : -1;
    }

    return _BettySoundBite;
}

void MCSoundSystem::SetMusicState(std::optional<MCMusicState> state)
{
    _MusicState.fill(false);

    if (state.has_value())
    {
        _MusicState[std::to_underlying(*state)] = true;
    }
}

void MCSoundSystem::FreeChannelResource(int32_t channel)
{
    if (const std::shared_ptr<MCSoundResource> resource = _ChannelResource[channel].lock(); resource != nullptr)
    {
        _Renderer.DestroyResource(*resource);
    }

    _ChannelResource[channel].reset();
}

void MCSoundSystem::SetChannelImage(int32_t channel, std::span<const uint8_t> image, std::optional<uint64_t> hdId)
{
    if (const std::shared_ptr<MCSoundResource> resource = _ChannelResource[channel].lock(); resource != nullptr)
    {
        _Renderer.DestroyResource(*resource);
    }

    _ChannelResource[channel] = _Renderer.CreateResource(image, hdId);
}

bool MCSoundSystem::QueueRadioMessage(std::unique_ptr<MCRadioMessage> message)
{
    RadioQueue.RemoveDuplicates(*message);

    if (message->Priority == 1)
    {
        RemoveCurrentMessage();
        RadioQueue.RemovePilot(*message->Pilot.lock());
    }

    // Port-only: handed over before the push, which may drop the message (OB-061); one refused is decoded for nothing.
    for (const std::vector<uint8_t>& fragment : message->Fragments)
    {
        MCHdAudio::Prefetch(fragment);
    }

    return RadioQueue.Push(std::move(message)) == nullptr;
}

std::shared_ptr<MCRadio> MCSoundSystem::AddRadio(std::unique_ptr<MCRadio> radio)
{
    return _Radios.emplace_back(std::move(radio));
}

void MCSoundSystem::StopStream(int32_t stream)
{
    const int32_t channel = MUSIC_CHANNEL_A + stream;
    _Renderer.Stop(channel);
    FreeChannelResource(channel);
    Streams[stream] = MCMusicStream{};
}

void MCSoundSystem::PurgeSoundSystem()
{
    if (!_SoundOn)
    {
        return;
    }

    for (int32_t stream = 0; stream < 2; stream++)
    {
        if (Streams[stream].Playing && !_ChannelResource[MUSIC_CHANNEL_A + stream].expired())
        {
            StopStream(stream);
        }
    }

    WholeMsgDone = true;
    RadioQueue.Clear();

    if (CurrentMessage != nullptr)
    {
        RemoveCurrentMessage();
    }

    for (int32_t i = 0; i < NUM_SAMPLE_CHANNELS; i++)
    {
        _Renderer.Stop(i);
        FreeChannelResource(i);
    }

    InCombat = 0;
    InContact = 0;
    FriendlyDestroyed = 0;
    EnemyDestroyed = 0;
    CurrentMusicId = -1;
    StopRain();
    _RainLevel = 0.0f;
    _RainMissing = false;
    _ThunderRolls.clear();
    _ThunderMissing = false;
    _ThunderBytes.reset();
    _ThunderPath.reset();
    _ThunderPcm.clear();
    // The radios go (their pilots keep pointers to them, as in the original), and the files they shared.
    _Radios.clear();

    for (MCSoundBite& bite : Sounds)
    {
        bite.BiteData = {};
    }

    _BiteHdIds.clear();
    NoiseFile.reset();
    RadioMessageInfoLoaded = false;
    _BettySoundBite = {};
    _BettyLoaded = -1;
}

void MCSoundSystem::PlayStaticNoise()
{
    if (UseSound == 0)
    {
        return;
    }

    if (NoiseFile == nullptr)
    {
        NoiseFile = std::make_unique<MCPacketFile>();

        if (NoiseFile->Open(GamePath(CDsoundPath, "noise", ".pak")) != 0)
        {
            return;
        }
    }

    NoiseFile->SeekPacket(2);

    if (_NoiseData.empty())
    {
        _NoiseData.resize(static_cast<size_t>(NoiseFile->GetPacketSize()));
    }

    NoiseFile->ReadPacket(2, std::span(_NoiseData).first(static_cast<size_t>(NoiseFile->GetPacketSize())));
    SetChannelImage(NOISE_CHANNEL, _NoiseData);
    // Marked to fade out at once: update lowers the static until it stops.
    SampleChannels[NOISE_CHANNEL].FadeDown = true;
    _Renderer.SetChannelPanning(NOISE_CHANNEL, 0.0f);
    _Renderer.SetChannelVolume(NOISE_CHANNEL, ChannelVolume(RadioLevel));
    _Renderer.SetChannelLooping(NOISE_CHANNEL, true);
    _Renderer.Play(NOISE_CHANNEL, _ChannelResource[NOISE_CHANNEL].lock());
}

void MCSoundSystem::StopStaticNoise()
{
    StopDigitalSample(NOISE_CHANNEL);
    _NoiseData = {};
}

void MCSoundSystem::UpdateStreamFade(int32_t stream)
{
    const int32_t channel = MUSIC_CHANNEL_A + stream;
    float& fade = Streams[stream].Fade;

    if (fade == 0.0f)
    {
        return;
    }

    float volume;

    if (fade < 0.0f)
    {
        fade = FrameLength + fade;

        if (fade >= 0.0f)
        {
            fade = 0.0f;
            _Renderer.Stop(channel);
            FreeChannelResource(channel);
            Streams[stream].Playing = false;
            return;
        }

        volume = std::fabs(fade) / StreamFadeDownTime * static_cast<float>(MusicLevel);
    }
    else
    {
        fade = fade - FrameLength;

        if (fade <= 0.0f)
        {
            fade = 0.0f;
            _Renderer.SetChannelVolume(channel, ChannelVolume(MusicLevel));
            return;
        }

        volume = (StreamFadeDownTime - std::fabs(fade)) / StreamFadeDownTime * static_cast<float>(MusicLevel);
    }

    volume = std::clamp(volume, 0.0f, 128.0f);
    _Renderer.SetChannelVolume(channel, ChannelVolume(static_cast<uint32_t>(static_cast<int32_t>(volume)) & 0xff));
}

void MCSoundSystem::DropEndedStream(int32_t stream)
{
    const int32_t channel = MUSIC_CHANNEL_A + stream;

    if (!Streams[stream].Playing || _ChannelResource[channel].expired() || _Renderer.IsPlaying(channel))
    {
        return;
    }

    FreeChannelResource(channel);
    Streams[stream] = MCMusicStream{};
    SetMusicState(std::nullopt);
    CurrentMusicId = -1;
}

void MCSoundSystem::UpdateRadio()
{
    // The current message: its fragments play one after another, the first after its static.
    if (CurrentMessage != nullptr && !_Renderer.IsPlaying(PILOT_SPEECH_CHANNEL))
    {
        // (Removing the message below ends it: nothing uses it after that.)
        const MCRadioMessage& message = *CurrentMessage;

        if (WholeMsgDone)
        {
            RemoveCurrentMessage();
        }
        else if (const std::span<const uint8_t> noise = message.NoiseAt(CurrentFragment);
                 !PlayingNoise && !noise.empty())
        {
            SetChannelImage(PILOT_SPEECH_CHANNEL, noise);
            _Renderer.SetChannelVolume(PILOT_SPEECH_CHANNEL, ChannelVolume(RadioLevel));
            _Renderer.Play(PILOT_SPEECH_CHANNEL, _ChannelResource[PILOT_SPEECH_CHANNEL].lock());
            PlayingNoise = true;
        }
        else
        {
            PlayingNoise = false;

            if (const std::span<const uint8_t> fragment = message.FragmentAt(CurrentFragment); fragment.empty())
            {
                WholeMsgDone = true;
            }
            else
            {
                SetChannelImage(PILOT_SPEECH_CHANNEL, fragment);
                _Renderer.SetChannelVolume(PILOT_SPEECH_CHANNEL, ChannelVolume(RadioLevel));
                _Renderer.Play(PILOT_SPEECH_CHANNEL, _ChannelResource[PILOT_SPEECH_CHANNEL].lock());
            }

            CurrentFragment++;
        }
    }

    // The next queued message starts with its first fragment's static.
    if (UseSound == 0 || RadioQueue.Size() == 0 || !WholeMsgDone)
    {
        return;
    }

    CurrentFragment = 0;
    MoveFromQueueToPlaying();
    const std::shared_ptr<MCTacticalMap> tacMap = TacticalMap();

    if (tacMap != nullptr && tacMap->IsHidden() == 0 && tacMap->DisplayType == MCTacmapPage::Map &&
        CurrentMessage->MovieWindow != nullptr)
    {
        tacMap->VideoWindow->SetStar(CurrentMessage->Pilot.lock());
    }

    FreeChannelResource(PILOT_SPEECH_CHANNEL);

    // The original made a memory resource of a message without static too (a null image: a crash in GetWaveInfo).
    if (const std::span<const uint8_t> noise = CurrentMessage->NoiseAt(0); noise.empty())
    {
        PlayingNoise = false;
    }
    else
    {
        _ChannelResource[PILOT_SPEECH_CHANNEL] = _Renderer.CreateResource(noise);
        PlayingNoise = true;
    }

    _Renderer.Play(PILOT_SPEECH_CHANNEL, _ChannelResource[PILOT_SPEECH_CHANNEL].lock());
    WholeMsgDone = false;
    const std::shared_ptr<MCGuiSmackerWindow> window = CurrentMessage->MovieWindow.Lock();

    if (window != nullptr && CurrentMessage->Movie != nullptr && tacMap->IsHidden() == 0 &&
        tacMap->DisplayType == MCTacmapPage::Map &&
        window->StartSmackerMovie(std::move(CurrentMessage->Movie), false) == 0)
    {
        // Shown in front of its depth (the original added it to the screen here).
        window->ShowGuiWindow(true);
        window->SetDepth(0x5a);
        window->Draw();
    }
}

void MCSoundSystem::UpdateMusicChoice()
{
    Scenario()->CheckAnyoneInCombat();
    const bool enemyCue = HasMusicState(MCMusicState::EnemyDestroyed);

    if (enemyCue && EnemyDestroyed != 0)
    {
        EnemyDestroyed = 0;
    }

    const bool friendlyCue = HasMusicState(MCMusicState::FriendlyDestroyed);

    if (friendlyCue && FriendlyDestroyed != 0)
    {
        FriendlyDestroyed = 0;
    }

    auto playCombat = [this]()
    {
        if (PlayDigitalMusic(static_cast<uint8_t>(RandomNumber(6) + 14), true) == 0)
        {
            SetMusicState(MCMusicState::Combat);
        }
    };

    auto playContact = [this]()
    {
        if (PlayDigitalMusic(static_cast<uint8_t>(RandomNumber(3) + 5), false) == 0)
        {
            SetMusicState(MCMusicState::Contact);
            InContact = 0;
        }
    };

    auto playAmbient = [this]()
    {
        if (PlayDigitalMusic(AMBIENT_MUSIC, true) == 0)
        {
            SetMusicState(MCMusicState::Ambient);
        }
    };

    if (CurrentMusicId == -1)
    {
        if (InCombat != 0)
        {
            playCombat();
        }
        else if (InContact != 0)
        {
            playContact();
        }
        else
        {
            playAmbient();
        }

        return;
    }

    if (HasMusicState(MCMusicState::Abl))
    {
        return;
    }

    if (EnemyDestroyed != 0 && !enemyCue && !friendlyCue)
    {
        if (PlayDigitalMusic(12, false) == 0)
        {
            SetMusicState(MCMusicState::EnemyDestroyed);
            EnemyDestroyed = 0;
        }
    }
    else if (FriendlyDestroyed != 0 && !friendlyCue && !enemyCue)
    {
        if (PlayDigitalMusic(13, false) == 0)
        {
            SetMusicState(MCMusicState::FriendlyDestroyed);
            FriendlyDestroyed = 0;
        }
    }
    else if (InCombat != 0)
    {
        if (!HasMusicState(MCMusicState::Combat) && !enemyCue && !friendlyCue)
        {
            playCombat();
        }
    }
    else if (HasMusicState(MCMusicState::Combat))
    {
        if (InDemo == 0)
        {
            playContact();
        }
        else
        {
            playAmbient();
        }
    }
}

void MCSoundSystem::Update()
{
    // Port-only: the rain bed and the thunder play whether or not the music does.
    const bool scenarioRuns = Mission() != nullptr && Mission()->State == MCMissionState::Scenario;
    const MCWeather::MCWeatherLook heard =
        UseSound != 0 && scenarioRuns ? MCWeather::HeardLook() : MCWeather::MCWeatherLook{};
    UpdateRain(heard.Heard);
    const std::optional<MCWeather::MCThunderClap> clap =
        UseSound != 0 && scenarioRuns ? MCWeather::TakeThunder() : std::nullopt;
    UpdateThunder(heard.ThunderHeard,
                  clap.has_value() ? MCOptionalRef<const MCWeather::MCThunderClap>(*clap) : std::nullopt);

    if (UseSound == 0 || UseMusic == 0)
    {
        return;
    }

    for (MCSampleChannel& channel : SampleChannels)
    {
        channel.InUse = false;
    }

    if (GlobalLog() != nullptr && !PilotLogisticsSpeech.empty() && !_Renderer.IsPlaying(PILOT_SPEECH_CHANNEL))
    {
        FreeChannelResource(PILOT_SPEECH_CHANNEL);
        PilotLogisticsSpeech = {};
    }

    UpdateRadio();

    if (Streams[0].Playing || Streams[1].Playing)
    {
        for (int32_t stream = 0; stream < 2; stream++)
        {
            if (Streams[stream].Playing)
            {
                UpdateStreamFade(stream);
            }
        }

        DropEndedStream(0);
        DropEndedStream(1);
    }

    if (Scenario() != nullptr && !Scenario()->MusicPending)
    {
        UpdateMusicChoice();
    }

    if (Scenario() != nullptr && SomethingOnFire != 0)
    {
        SomethingOnFire = 0;

        if (!_Renderer.IsPlaying(NOISE_CHANNEL))
        {
            if (static_cast<size_t>(FIRE_BITE) >= Sounds.size() ||
                (Sounds[FIRE_BITE].BiteData.empty() && !PreloadSoundBite(FIRE_BITE)))
            {
                return;
            }

            SetChannelImage(NOISE_CHANNEL, Sounds[FIRE_BITE].BiteData, BiteHdId(FIRE_BITE));
            _Renderer.SetChannelLooping(NOISE_CHANNEL, true);
            _Renderer.SetChannelPanning(NOISE_CHANNEL, 0.0f);
            // OB-181 fixed: the original played the fire at the effects volume, without the bite's own.
            _Renderer.SetChannelVolume(NOISE_CHANNEL,
                                       ChannelVolume(DigitalMasterVolume) * SoundBiteGain(Sounds[FIRE_BITE].Volume));
            SampleChannels[NOISE_CHANNEL].SampleId = FIRE_BITE;
            _Renderer.Play(NOISE_CHANNEL, _ChannelResource[NOISE_CHANNEL].lock());
        }
    }
    else if (_Renderer.IsPlaying(NOISE_CHANNEL))
    {
        StopDigitalSample(NOISE_CHANNEL);
    }

    // The camera-placed effects stop once the camera is out of range.
    if (Scenario() != nullptr)
    {
        for (int32_t channel = 11; channel < 14; channel++)
        {
            const float dx = SampleChannels[channel].X - Eye()->Position.X;
            const float dy = SampleChannels[channel].Y - Eye()->Position.Y;

            if (_Renderer.IsPlaying(channel) && MaxSoundDistance * MaxSoundDistance <= dx * dx + dy * dy)
            {
                StopDigitalSample(channel);
            }
        }
    }

    for (int32_t channel = 0; channel < NUM_SAMPLE_CHANNELS; channel++)
    {
        MCSampleChannel& state = SampleChannels[channel];

        if (!_Renderer.IsPlaying(channel))
        {
            state.FadeDown = false;
            state.SampleId = -1;
            continue;
        }

        if (!state.FadeDown)
        {
            continue;
        }

        const float volume = std::max(_Renderer.ChannelVolume(channel), 0.015625f);
        _Renderer.SetChannelVolume(channel, volume - 0.015625f);

        if (_Renderer.ChannelVolume(channel) == 0.0f)
        {
            state.FadeDown = false;
            _Renderer.Stop(channel);
            FreeChannelResource(channel);
        }
    }
}

void MCSoundSystem::UpdateRain(float heard)
{
    const float step = FrameLength / RainFade;
    _RainLevel = heard > _RainLevel ? std::min(heard, _RainLevel + step) : std::max(heard, _RainLevel - step);

    if (_RainLevel <= 0.0f || _RainMissing)
    {
        StopRain();
        return;
    }

    if (_RainStream == nullptr)
    {
        const std::optional<std::filesystem::path> path = MCHdAudio::RainLoop();
        _RainLoop = path.has_value() ? MCFlac::Open(*path) : nullptr;
        _RainStream = StreamPackSound(_Renderer.Mixer(), path, "rain loop", 1.0f, _RainLoop);

        if (_RainStream == nullptr)
        {
            _RainMissing = true;
            StopRain();
            return;
        }

        _RainChunk.resize(PackChunkFrames * _RainLoop->Format().BlockAlign());
    }

    _RainStream->SetVolume(MCSoundChannel::BufferVolume(ChannelVolume(DigitalMasterVolume) * _RainLevel));
    const uint64_t ahead = uint64_t{PackAheadSeconds} * _RainLoop->Format().Rate;
    bool fromStart = false;

    // Round and round: at the loop's end, from its start again (the loop is made to run on without a seam).
    while (_RainStream->QueuedFrames() < ahead)
    {
        const size_t read = _RainLoop->Read(_RainChunk);
        _RainStream->Queue(std::span(_RainChunk).first(read));

        if (read == _RainChunk.size())
        {
            fromStart = false;
            continue;
        }

        // Nothing read from its start, or no way back to it: a file cut short.
        if ((read == 0 && fromStart) || !_RainLoop->Seek(0))
        {
            SDL_Log("MCSoundSystem: the rain loop stopped reading; no rain heard till the mission ends");
            _RainMissing = true;
            StopRain();
            return;
        }

        fromStart = true;
    }
}

void MCSoundSystem::StopRain()
{
    _RainStream.reset();
    _RainLoop.reset();
}

void MCSoundSystem::UpdateThunder(float heard, MCOptionalRef<const MCWeather::MCThunderClap> clap)
{
    if (clap && heard > 0.0f && !_ThunderMissing)
    {
        StartThunder(heard, clap->get());
    }

    const float fade = FrameLength / ThunderFadeOut;

    for (MCThunderRoll& roll : _ThunderRolls)
    {
        roll.Left = roll.Fading ? std::max(roll.Left - fade, 0.0f) : roll.Left;
        roll.Stream->SetVolume(
            MCSoundChannel::BufferVolume(ChannelVolume(DigitalMasterVolume) * roll.Level * roll.Left));
        const uint64_t ahead = uint64_t{PackAheadSeconds} * RollFormat(roll).Rate;

        while (!roll.Read && roll.Stream->QueuedFrames() < ahead)
        {
            // From the thunder decoded as the mission loaded, in the chunks a reader gives: the same sound, nothing
            // decoded on the game's thread.
            if (roll.File == nullptr)
            {
                const size_t queued = std::min(_ThunderChunk.size(), _ThunderPcm.size() - roll.PcmAt);
                roll.Stream->Queue(std::span(_ThunderPcm).subspan(roll.PcmAt, queued));
                roll.PcmAt += queued;
                roll.Read = queued < _ThunderChunk.size() || roll.PcmAt == _ThunderPcm.size();
                continue;
            }

            const size_t read = roll.File->Read(_ThunderChunk);
            roll.Stream->Queue(std::span(_ThunderChunk).first(read));
            roll.Read = read < _ThunderChunk.size();
        }
    }

    // A clap played out, or faded out under a newer one, is done.
    std::erase_if(_ThunderRolls, [](const MCThunderRoll& roll)
                  { return (roll.Read && roll.Stream->QueuedFrames() == 0) || roll.Left <= 0.0f; });
}

void MCSoundSystem::ReadThunder()
{
    if (_ThunderBytes == nullptr && !_ThunderMissing)
    {
        _ThunderPath = MCHdAudio::Thunder();
        _ThunderBytes = _ThunderPath.has_value() ? MCFlac::ReadFile(*_ThunderPath) : nullptr;
    }
}

void MCSoundSystem::PrepareThunder()
{
    if (UseSound != 0 && MCWeather::LookOf(MCWeather::Shown()).Strikes > 0.0f)
    {
        ReadThunder();
        DecodeThunder();
    }
}

void MCSoundSystem::DecodeThunder()
{
    if (_ThunderBytes == nullptr || !_ThunderPcm.empty())
    {
        return;
    }

    const std::unique_ptr<MCFlac> file = MCFlac::Open(_ThunderBytes);

    if (file == nullptr || file->Frames() == 0 || file->Format().BlockAlign() == 0 ||
        file->Frames() > ThunderMostBytes / file->Format().BlockAlign())
    {
        return;
    }

    // As a reader gives it: a corrupt frame ends it there, as it ends a streamed clap.
    _ThunderPcm.resize(static_cast<size_t>(file->Frames()) * file->Format().BlockAlign());
    _ThunderPcm.resize(file->Read(_ThunderPcm));
    _ThunderFormat = file->Format();
}

const MCSoundFormat& MCSoundSystem::RollFormat(const MCThunderRoll& roll) const
{
    return roll.File != nullptr ? roll.File->Format() : _ThunderFormat;
}

void MCSoundSystem::StartThunder(float heard, const MCWeather::MCThunderClap& clap)
{
    // The thunder is read once (each clap's reader reads it from memory), till the purge; decoded as the mission
    // loaded (PrepareThunder), a clap plays that.
    ReadThunder();
    MCThunderRoll roll;
    const float speed = std::lerp(ThunderNearSpeed, ThunderFarSpeed, clap.Far);

    if (!_ThunderPcm.empty())
    {
        std::expected<std::shared_ptr<MCAudioStream>, std::string> stream =
            StreamAtSpeed(_Renderer.Mixer(), _ThunderFormat, speed);
        roll.Stream = stream.has_value() ? std::move(*stream) : nullptr;

        if (!stream.has_value())
        {
            SDL_Log("MCSoundSystem: the decoded thunder won't play: %s", stream.error().c_str());
        }
    }
    else
    {
        roll.File = MCFlac::Open(_ThunderBytes);
        roll.Stream = StreamPackSound(_Renderer.Mixer(), _ThunderPath, "thunder", speed, roll.File);
    }

    if (roll.Stream == nullptr)
    {
        _ThunderMissing = true;
        return;
    }

    for (MCThunderRoll& older : _ThunderRolls)
    {
        older.Fading = true;
    }

    _ThunderChunk.resize(PackChunkFrames * RollFormat(roll).BlockAlign());
    roll.Level = heard * std::lerp(1.0f, ThunderFarLevel, clap.Far);
    roll.Stream->SetPan(static_cast<int32_t>(std::lround(clap.Side * ThunderPanMost)));
    _ThunderRolls.push_back(std::move(roll));
}

int32_t MCSoundSystem::PlayDigitalMusic(int32_t musicId, bool loop)
{
    if (UseMusic == 0 || musicId < 0 || musicId >= std::ssize(DigitalMusicIds))
    {
        return 0;
    }

    if (DigitalMusicIds[musicId].starts_with("NONE"))
    {
        return 0;
    }

    if (musicId < 5)
    {
        _MusicState[std::to_underlying(MCMusicState::Low)] = true;
    }

    if (musicId == CurrentMusicId)
    {
        return MUSIC_BUSY;
    }

    if (Streams[0].Fade != 0.0f && Streams[1].Fade != 0.0f)
    {
        return MUSIC_BUSY;
    }

    if (UseSound == 0)
    {
        return 0;
    }

    if (CurPlanet == 1)
    {
        musicId += SECOND_PLANET_MUSIC;
    }

    // Which stream takes the new music; the other one fades out.
    int32_t stream;

    if (!Streams[0].Playing)
    {
        stream = 0;
    }
    else
    {
        if (Streams[1].Playing)
        {
            return MUSIC_BUSY;
        }

        stream = 1;
    }

    const std::string musicName = GamePath(SoundPath, DigitalMusicIds[musicId], ".wav");

    if (FileExists(musicName))
    {
        const int32_t channel = MUSIC_CHANNEL_A + stream;

        if (const std::shared_ptr<MCSoundResource> resource = _ChannelResource[channel].lock(); resource != nullptr)
        {
            _Renderer.DestroyResource(*resource);
        }

        _ChannelResource[channel] = _Renderer.CreateResource(MCSoundResourceType::Stream, musicName);

        if (stream == 0)
        {
            if (Streams[1].Playing)
            {
                Streams[1].Fade = -StreamFadeDownTime;
            }

            Streams[0].Fade = StreamFadeDownTime;
            Streams[0].Playing = true;
        }
        else
        {
            Streams[0].Fade = -StreamFadeDownTime;
            Streams[1].Fade = StreamFadeDownTime;
            Streams[1].Playing = true;
        }

        _Renderer.SetChannelVolume(channel, 0.0f);
        _Renderer.SetChannelPanning(channel, 0.0f);
        _Renderer.SetChannelLooping(channel, loop);
        _Renderer.Play(channel, _ChannelResource[channel].lock());
        CurrentMusicId = musicId;
    }

    return 0;
}

int32_t MCSoundSystem::PlayBettySample(uint32_t bettyId)
{
    if (UseSound == 0 || bettyId >= BETTY_SAMPLES)
    {
        return -1;
    }

    const std::span<const uint8_t> sample = LoadBettySample(static_cast<int32_t>(bettyId));

    if (sample.empty() || !IsWaveImage(sample))
    {
        return -1;
    }

    SetChannelImage(BETTY_CHANNEL, sample, BettyHdId());
    SampleChannels[BETTY_CHANNEL].FadeDown = false;
    _Renderer.SetChannelPanning(BETTY_CHANNEL, 0.0f);
    _Renderer.SetChannelVolume(BETTY_CHANNEL, ChannelVolume(RadioLevel));
    SampleChannels[BETTY_CHANNEL].SampleId = static_cast<int32_t>(bettyId);
    _Renderer.Play(BETTY_CHANNEL, _ChannelResource[BETTY_CHANNEL].lock());
    return BETTY_CHANNEL;
}

bool MCSoundSystem::IsSamplePlaying(int32_t sampleId) const
{
    return std::ranges::contains(SampleChannels, sampleId, &MCSampleChannel::SampleId);
}

bool MCSoundSystem::IsChannelPlaying(int32_t channel) const
{
    if (channel < 0 || channel > 16)
    {
        return false;
    }

    return _Renderer.IsPlaying(channel);
}

int32_t MCSoundSystem::PlayDigitalSample(uint32_t sampleId, uint32_t, MCOptionalRef<MCGameObject> source, bool atCamera,
                                         bool farRange)
{
    return PlaySample(sampleId, source, atCamera, farRange, DigitalMasterVolume);
}

int32_t MCSoundSystem::PlayInterfaceSample(uint32_t sampleId)
{
    return PlaySample(sampleId, std::nullopt, false, false, InterfaceLevel);
}

int32_t MCSoundSystem::PlaySample(uint32_t sampleId, MCOptionalRef<MCGameObject> source, bool atCamera, bool farRange,
                                  uint8_t level)
{
    if (UseSound == 0 || IsSamplePlaying(static_cast<int32_t>(sampleId)) || sampleId >= Sounds.size())
    {
        return -1;
    }

    float listenerX = 0.0f;
    float listenerY = 0.0f;

    if (Scenario() != nullptr && Eye() != nullptr)
    {
        listenerX = Eye()->Position.X;
        listenerY = Eye()->Position.Y;
    }

    float soundX = listenerX;
    float soundY = listenerY;

    if (source.has_value() && !atCamera)
    {
        const MCVector3D position = source->get().GetPosition();
        soundX = position.X;
        soundY = position.Y;
    }

    const float dx = soundX - listenerX;
    const float dy = soundY - listenerY;
    const float rangeScale = farRange ? 15.0f : 1.0f;

    if (dx * dx + dy * dy > rangeScale * MaxSoundDistance * rangeScale * MaxSoundDistance)
    {
        return -1;
    }

    const MCSoundBite& bite = Sounds[sampleId];

    if (bite.BiteData.empty() && (!PreloadSoundBite(static_cast<int32_t>(sampleId)) || bite.BiteData.empty()))
    {
        return -1;
    }

    // Effects at a place take the first free channel of 1..9; effects at the camera 11..13, once each.
    int32_t channel;

    if (!atCamera)
    {
        for (channel = 1;; channel++)
        {
            if (channel > 9)
            {
                return -1;
            }

            if (!_Renderer.IsPlaying(channel) && !SampleChannels[channel].InUse)
            {
                break;
            }
        }
    }
    else
    {
        for (channel = 11;; channel++)
        {
            if (channel > 13)
            {
                return -1;
            }

            if (!_Renderer.IsPlaying(channel) && !SampleChannels[channel].InUse)
            {
                break;
            }

            if (static_cast<uint32_t>(SampleChannels[channel].SampleId) == sampleId)
            {
                return -1;
            }
        }
    }

    MCSampleChannel& state = SampleChannels[channel];
    state.InUse = true;
    state.X = soundX;
    state.Y = soundY;
    state.FadeDown = false;
    int32_t pan = 0x40;

    if (listenerX != soundX || listenerY != soundY)
    {
        pan = SoundPanPosition(dx, dy);
    }

    _Renderer.SetChannelPanning(channel, (static_cast<float>(pan) - 64.0f) * (1.0f / 128.0f));
    _Renderer.SetChannelVolume(channel, ChannelVolume(level) * SoundBiteGain(bite.Volume));
    state.SampleId = static_cast<int32_t>(sampleId);

    // The original kept the destroyed resource's pointer when the bite isn't a wave; the port forgets it.
    FreeChannelResource(channel);

    if (const std::span<const uint8_t> wave = Sounds[sampleId].BiteData; !wave.empty() && IsWaveImage(wave))
    {
        _ChannelResource[channel] = _Renderer.CreateResource(wave, BiteHdId(static_cast<int32_t>(sampleId)));
        _Renderer.Play(channel, _ChannelResource[channel].lock());
    }

    return channel;
}

void MCSoundSystem::StopDigitalSample(int32_t channel)
{
    if (UseSound == 0)
    {
        return;
    }

    if (_Renderer.IsPlaying(channel))
    {
        SampleChannels[channel].FadeDown = true;
        SampleChannels[channel].SampleId = -1;
    }
}

void MCSoundSystem::StopDigitalMusic()
{
    if (UseSound == 0)
    {
        return;
    }

    StopStream(0);
    StopStream(1);
    CurrentMusicId = -1;
}

int32_t MCSoundSystem::PlayPilotSpeech(std::string_view fileName, int32_t speechId)
{
    if (GlobalLog() == nullptr || !PilotLogisticsSpeech.empty())
    {
        return 0;
    }

    MCPacketFile speechFile;
    int32_t result = speechFile.Open(GamePath(CDsoundPath, fileName, ".pak"));

    if (result != 0)
    {
        return result;
    }

    result = speechFile.SeekPacket(speechId);

    if (result != 0)
    {
        return result;
    }

    PilotLogisticsSpeech.assign(static_cast<size_t>(speechFile.GetPacketSize()), 0);
    FreeChannelResource(PILOT_SPEECH_CHANNEL);
    speechFile.ReadPacket(speechId, PilotLogisticsSpeech);
    _ChannelResource[PILOT_SPEECH_CHANNEL] = _Renderer.CreateResource(PilotLogisticsSpeech);
    SampleChannels[PILOT_SPEECH_CHANNEL].FadeDown = false;
    _Renderer.SetChannelPanning(PILOT_SPEECH_CHANNEL, 0.0f);
    _Renderer.SetChannelVolume(PILOT_SPEECH_CHANNEL, ChannelVolume(RadioLevel));
    _Renderer.Play(PILOT_SPEECH_CHANNEL, _ChannelResource[PILOT_SPEECH_CHANNEL].lock());
    CurrentPilotSpeech = speechId;
    speechFile.Close();
    return 0;
}

void MCSoundSystem::PlayAblDigitalMusic(int32_t musicId)
{
    if (HasMusicState(MCMusicState::Abl) || musicId < 0 || musicId >= std::ssize(DigitalMusicIds))
    {
        return;
    }

    if (PlayDigitalMusic(musicId, false) == 0)
    {
        SetMusicState(MCMusicState::Abl);
    }
}

void MCSoundSystem::StopAblMusic()
{
    if (UseSound == 0)
    {
        return;
    }

    StopDigitalMusic();
}

void MCSoundSystem::PlayAblsfx(int32_t sfxId)
{
    PlayDigitalSample(static_cast<uint32_t>(sfxId), 1, std::nullopt, false, false);
}

void MCSoundSystem::PlayAblVideo(int32_t videoId)
{
}

void MCSoundSystem::MoveFromQueueToPlaying()
{
    RemoveCurrentMessage();
    CurrentMessage = RadioQueue.PopFront();
}

void MCSoundSystem::RemoveCurrentMessage()
{
    if (CurrentMessage != nullptr)
    {
        if (CurrentMessage->MovieWindow != nullptr)
        {
            CurrentMessage->MovieWindow->EndSmackerMovie();

            if (TacticalMap() != nullptr)
            {
                TacticalMap()->VideoWindow->SetStar(nullptr);
            }

            CurrentMessage->MovieWindow.reset();
            CurrentMessage->Movie.reset();
        }

        CurrentMessage.reset();
    }

    FreeChannelResource(PILOT_SPEECH_CHANNEL);
    _Renderer.Stop(PILOT_SPEECH_CHANNEL);
    WholeMsgDone = true;
}

std::shared_ptr<MCSoundSystem> SoundSystem()
{
    return MCGameContext::Current().SoundSystem();
}
