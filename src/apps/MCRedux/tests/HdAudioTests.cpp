#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCNullAudioDevice.h"
#include "gameos/MCSoundChannel.h"
#include "gameos/MCSoundRenderer.h"
#include "gameos/MCSoundResource.h"
#include "lib/MCPacketFile.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "platform/MCFlac.h"
#include "sound/MCHdAudio.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCWeather.h"

namespace
{
    // libFLAC 1.4.3: 960 stereo frames at 48000 Hz, signed PCM16 (1000 left, -2000 right).
    std::vector<uint8_t> Flac()
    {
        const std::string hex =
            "664c614300000022100010000000100000100bb802f0000003c09bf6bd9cc5a10655125d927e2b6ff8f68400002820000000726566"
            "6572656e6365206c6962464c414320312e342e3320323032333036323300000000fff87a180003bfab01207d011f838b4a";
        std::vector<uint8_t> bytes;

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        }

        return bytes;
    }

    std::vector<uint8_t> Original(uint32_t frames = 441)
    {
        std::vector<uint8_t> wave;
        const auto word = [&wave](uint32_t value, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
            {
                wave.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
        };

        const auto tag = [&wave](std::string_view text) { wave.insert(wave.end(), text.begin(), text.end()); };
        tag("RIFF");
        word(36 + frames, 4);
        tag("WAVEfmt ");
        word(16, 4);
        word(1, 2);
        word(1, 2);
        word(22050, 4);
        word(22050, 4);
        word(1, 2);
        word(8, 2);
        tag("data");
        word(frames, 4);
        wave.resize(44 + frames, 128);
        return wave;
    }

    void Write(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    /// <summary>A pack in its own temporary folder with HD audio on; the folder, setting and pack go back after.</summary>
    struct Fixture
    {
        int PreviousSetting = GHdAudio;
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() /
            std::format("mc_tests_hd_audio_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        Fixture()
        {
            GHdAudio = HdAudioOn;
            MCHdArt::SetPackFolder(Root);
            Config();
        }

        ~Fixture()
        {
            GHdAudio = PreviousSetting;
            MCHdArt::SetPackFolder({});
            MCHdAudio::Forget();
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        /// <summary>Writes the pack's audio.cfg of <paramref name="format"/> and forgets what was found.</summary>
        void Config(int format = 1)
        {
            const std::string cfg = std::format("FITini\n[HDAudioPack]\nl Format={}\nFITend\n", format);
            Write(Root / "audio/audio.cfg", std::span(reinterpret_cast<const uint8_t*>(cfg.data()), cfg.size()));
            MCHdAudio::Forget();
        }

        std::filesystem::path Sample(std::span<const uint8_t> wave)
        {
            return Root / "audio/samples" / (MCHdAudio::SampleKey(wave) + ".flac");
        }
    };
}

TEST_CASE("HD audio: FLAC decoding, seeking and corrupt input")
{
    Fixture fixture;
    const auto path = fixture.Root / "fixture.flac";
    Write(path, Flac());
    auto flac = MCFlac::Open(path);
    CHECK(flac != nullptr);

    if (!flac)
    {
        return;
    }

    CHECK_EQ(flac->Frames(), uint64_t{960});
    CHECK_EQ(flac->Format().Rate, uint32_t{48000});
    CHECK_EQ(flac->Format().Channels, uint16_t{2});
    CHECK(flac->Validate());
    CHECK(flac->CheckEnd());
    std::array<uint8_t, 4> first{};
    CHECK_EQ(flac->Read(first), size_t{4});
    CHECK_EQ(first[0], uint8_t{0xe8});
    CHECK(flac->Seek(959));
    std::array<uint8_t, 8> pcm{};
    CHECK_EQ(flac->Read(pcm), size_t{4});
    CHECK_EQ(pcm[0], uint8_t{0xe8});
    CHECK_EQ(pcm[1], uint8_t{0x03});
    CHECK_EQ(pcm[2], uint8_t{0x30});
    CHECK_EQ(pcm[3], uint8_t{0xf8});
    CHECK(!flac->Seek(960));
    auto broken = Flac();
    broken.resize(broken.size() - 3);
    Write(path, broken);
    // The header is whole, so the file opens; both checks find its end missing.
    flac = MCFlac::Open(path);
    REQUIRE(flac != nullptr);
    CHECK(!flac->Validate());
    CHECK(!flac->CheckEnd());
    Write(path, Original());
    CHECK(MCFlac::Open(path) == nullptr);
}

TEST_CASE("HD audio: PCM identity ignores unrelated trailing allocation bytes")
{
    auto wave = Original();
    const auto key = MCHdAudio::SampleKey(wave);
    CHECK_EQ(key.size(), size_t{16});
    wave.resize(wave.size() + 40, 99);
    CHECK_EQ(MCHdAudio::SampleKey(wave), key);
    wave[44] = 129;
    CHECK(MCHdAudio::SampleKey(wave) != key);
    CHECK(MCHdAudio::SampleKey({}).empty());
}

TEST_CASE("HD audio: sample substitution keeps duration, pan, volume, frequency and loops")
{
    Fixture fixture;
    Write(fixture.Sample(Original()), Flac());
    MCSoundResource resource(Original());
    CHECK_EQ(resource.Format.nSamplesPerSec, uint32_t{48000});
    CHECK_EQ(resource.Format.nChannels, uint16_t{2});
    CHECK_EQ(resource.WaveSize, uint32_t{3840});
    auto mixer = MCAudio::CreateSilent();
    auto buffer = mixer->CreateBuffer({48000, 2, 16}, resource.WaveData());
    CHECK(buffer.has_value());

    if (!buffer)
    {
        return;
    }

    (*buffer)->SetVolume(-600);
    (*buffer)->SetPan(-10000);
    (*buffer)->SetFrequency(96000);
    (*buffer)->Play(false);
    std::vector<float> out(962);
    mixer->Render(out);
    CHECK(!(*buffer)->IsPlaying());
    CHECK(out[0] > 0.0f);
    CHECK_EQ(out[1], 0.0f);
    (*buffer)->SetFrequency(0);
    (*buffer)->Play(true);
    mixer->Render(out);
    CHECK((*buffer)->IsPlaying());
}

TEST_CASE("HD audio: missing, future, corrupt and wrong-length entries keep the original")
{
    Fixture fixture;
    Write(fixture.Sample(Original()), {});
    MCSoundResource missing(Original());
    CHECK_EQ(missing.WaveSize, uint32_t{441});
    Write(fixture.Sample(Original()), Flac());
    fixture.Config(2);
    MCSoundResource future(Original());
    CHECK_EQ(future.WaveSize, uint32_t{441});
    fixture.Config();
    Write(fixture.Sample(Original(882)), Flac());
    MCSoundResource wrong(Original(882));
    CHECK_EQ(wrong.Format.nSamplesPerSec, uint32_t{22050});
    auto broken = Flac();
    broken.resize(broken.size() - 4);
    Write(fixture.Sample(Original()), broken);
    MCHdAudio::Forget();
    MCSoundResource corrupt(Original());
    CHECK_EQ(corrupt.WaveSize, uint32_t{441});
}

TEST_CASE("HD audio: music selection, streaming silence and short seamless loops")
{
    Fixture fixture;
    const auto source = fixture.Root / "MUSIC02.WAV";
    Write(source, Original());
    const auto music = fixture.Root / "audio/music/stereo/MUSIC02.flac";
    Write(music, Flac());
    CHECK(MCHdAudio::Music("data\\sound\\music02.wav").has_value());
    CHECK(!MCHdAudio::Music("../../arbitrary.wav").has_value());
    MCSoundResource stream(MCSoundResourceType::Stream, source.string());
    CHECK_EQ(stream.Format.nSamplesPerSec, uint32_t{48000});
    CHECK_EQ(stream.DurationMs, uint32_t{20});
    CHECK_EQ(stream.WaveSize, uint32_t{0});
    std::vector<uint8_t> block(3840 * 5);
    CHECK_EQ(stream.Read(block, true), uint32_t{19200});

    for (size_t i = 0; i < block.size(); i += 4)
    {
        CHECK_EQ(block[i], uint8_t{0xe8});
        CHECK_EQ(block[i + 2], uint8_t{0x30});
    }

    stream.Rewind();
    stream.Read(block, false);
    CHECK_EQ(block[3840], uint8_t{0});
    // ORCHESTRAL takes the pack's orchestral music where it has some, its stereo music elsewhere; OFF the original.
    // The preferences offer ORCHESTRAL only once the pack has orchestral music.
    GHdAudio = HdAudioOrchestral;
    CHECK(!MCHdAudio::HasOrchestral());
    CHECK_EQ(MCHdAudio::Music(source.string())->string(), music.string());
    Write(fixture.Root / "audio/music/orchestral/MUSIC02.flac", Flac());
    MCHdAudio::Forget();
    CHECK(MCHdAudio::HasOrchestral());
    CHECK(MCHdAudio::Music(source.string())->parent_path().filename() == "orchestral");
    GHdAudio = HdAudioOff;
    CHECK(!MCHdAudio::Music(source.string()).has_value());
    MCSoundResource original(MCSoundResourceType::Stream, source.string());
    CHECK_EQ(original.Format.nSamplesPerSec, uint32_t{22050});
}

TEST_CASE("HD audio: an absent audio configuration disables otherwise valid replacements")
{
    Fixture fixture;
    const auto disabled = fixture.Root / "no_config";
    Write(disabled / "audio/samples" / (MCHdAudio::SampleKey(Original()) + ".flac"), Flac());
    Write(disabled / "audio/music/stereo/MUSIC02.flac", Flac());
    MCHdArt::SetPackFolder(disabled);
    CHECK(!MCHdAudio::PackReadable());
    CHECK(MCHdAudio::Sample(Original()) == nullptr);
    CHECK(!MCHdAudio::Music("MUSIC02.WAV").has_value());
    MCSoundResource resource(Original());
    CHECK_EQ(resource.Format.nSamplesPerSec, uint32_t{22050});
    CHECK_EQ(resource.WaveSize, uint32_t{441});
}

TEST_CASE("HD audio: corrupt and wrong-duration music fall back before playback")
{
    Fixture fixture;
    const auto source = fixture.Root / "MUSIC02.WAV";
    const auto music = fixture.Root / "audio/music/stereo/MUSIC02.flac";
    Write(source, Original());
    auto corrupt = Flac();
    corrupt.resize(corrupt.size() - 4);
    Write(music, corrupt);
    MCSoundResource broken(MCSoundResourceType::Stream, source.string());
    CHECK_EQ(broken.Format.nSamplesPerSec, uint32_t{22050});
    CHECK_EQ(broken.DurationMs, uint32_t{20});
    Write(music, Flac());
    Write(source, Original(882));
    MCSoundResource wrongLength(MCSoundResourceType::Stream, source.string());
    CHECK_EQ(wrongLength.Format.nSamplesPerSec, uint32_t{22050});
    CHECK_EQ(wrongLength.DurationMs, uint32_t{40});
}

TEST_CASE("HD audio: OFF plays the game's own sounds with a pack in place")
{
    Fixture fixture;
    Write(fixture.Sample(Original()), Flac());
    const auto source = fixture.Root / "MUSIC02.WAV";
    Write(source, Original());
    Write(fixture.Root / "audio/music/stereo/MUSIC02.flac", Flac());
    GHdAudio = HdAudioOff;
    CHECK(MCHdAudio::PackReadable());
    CHECK(MCHdAudio::Sample(Original()) == nullptr);
    MCSoundResource sample(Original());
    CHECK_EQ(sample.Format.nSamplesPerSec, uint32_t{22050});
    CHECK_EQ(sample.WaveSize, uint32_t{441});
    MCSoundResource stream(MCSoundResourceType::Stream, source.string());
    CHECK_EQ(stream.Format.nSamplesPerSec, uint32_t{22050});
}

TEST_CASE("HD audio: a sample played again comes from memory, and a wave without one is remembered too")
{
    Fixture fixture;
    Write(fixture.Sample(Original()), Flac());
    const auto first = MCHdAudio::Sample(Original());
    REQUIRE(first != nullptr);
    CHECK_EQ(first->Pcm.size(), size_t{3840});

    // With the file gone, the decoded copy still plays: nothing is read from disk again.
    std::filesystem::remove(fixture.Sample(Original()));
    CHECK(MCHdAudio::Sample(Original()) == first);
    MCSoundResource again(Original());
    CHECK_EQ(again.WaveSize, uint32_t{3840});

    // A wave the pack has nothing for stays without one when a file appears, until the pack is looked at again.
    CHECK(MCHdAudio::Sample(Original(882)) == nullptr);
    Write(fixture.Sample(Original(882)), Flac());
    CHECK(MCHdAudio::Sample(Original(882)) == nullptr);
    MCHdAudio::Forget();
    CHECK(MCHdAudio::Sample(Original()) == nullptr);
}

TEST_CASE("HD audio: HD music streams through a sound channel's ring, looping")
{
    Fixture fixture;
    const auto source = fixture.Root / "MUSIC02.WAV";
    Write(source, Original());
    Write(fixture.Root / "audio/music/stereo/MUSIC02.flac", Flac());
    MCTestContextScope scope;
    MCNullAudioDevice& device = scope.Context().SetAudio(std::make_unique<MCNullAudioDevice>());
    MCSoundRenderer& renderer = MCSoundRenderer::Install(2);
    const std::shared_ptr<MCSoundResource> music =
        renderer.CreateResource(MCSoundResourceType::Stream, source.string());
    REQUIRE(music != nullptr);
    CHECK_EQ(music->Format.nSamplesPerSec, uint32_t{48000});
    CHECK_EQ(music->Format.nChannels, uint16_t{2});

    // The ring holds seconds of a 20 ms track: it is filled by looping, and serviced as it plays.
    renderer.SetChannelLooping(1, true);
    renderer.Play(1, music);
    CHECK_CALLED(device.Played, 1);
    CHECK(renderer.IsPlaying(1));

    for (int32_t update = 0; update < 10; ++update)
    {
        renderer.ServiceStreams();
    }

    CHECK(renderer.IsPlaying(1));
    renderer.Stop(1);
    renderer.DestroyResource(*music);
    MCSoundRenderer::Uninstall();
}

/// <summary>
/// HD weather's rain bed: the pack's rain loop plays round and round, never running dry, at the effects volume as loud
/// as the rain is heard, easing in and out over the frames' time; whatever HD audio is set to, as the rain is an option
/// of its own. Without the pack's loop there is none till the purge. A downpour is heard louder than rain, and no rain
/// at all while none is drawn (here, without the GPU).
/// </summary>
TEST_CASE(
    "HD audio: the rain bed streams the pack's rain loop round and round at the effects volume, easing in and out")
{
    Fixture fixture;
    const std::filesystem::path loop = fixture.Root / "audio/ambience/rain.flac";
    Write(loop, Flac());
    GHdAudio = HdAudioOff;
    MCTestContextScope scope;
    scope.Context().SetAudio(std::make_unique<MCNullAudioDevice>());
    scope.Context().SetSoundSystem(std::make_unique<MCSoundSystem>());
    const std::shared_ptr<MCSoundSystem> sound = SoundSystem();
    MCAudio& mixer = SoundRenderer()->Mixer();
    sound->DigitalMasterVolume = 96;
    const float savedFrame = FrameLength;
    // A quarter of the way a frame (RainFade is 2 s).
    FrameLength = 0.5f;
    // The loop's left channel (its right is twice as loud the other way), 16-bit.
    constexpr float LoopLeft = 1000.0f / 32768.0f;
    constexpr size_t Frames = 12000;
    std::vector<float> out(2 * Frames);

    // As loud as the effects volume and the level make a channel, every frame: the 20 ms loop came round many times.
    const auto heardAt = [&](float level)
    {
        const float gain = MCAudio::GainFromHundredthsDb(
            MCSoundChannel::BufferVolume(static_cast<float>(sound->DigitalMasterVolume) / 128.0f * level));
        mixer.Render(out);
        size_t right = 0;

        for (size_t frame = 0; frame < Frames; ++frame)
        {
            right += std::abs(out[2 * frame] - LoopLeft * gain) <= LoopLeft * gain * 0.01f &&
                     std::abs(out[2 * frame + 1] + 2.0f * out[2 * frame]) <= LoopLeft * gain * 0.01f;
        }

        return right == Frames;
    };

    const auto silent = [&]
    {
        mixer.Render(out);
        return std::ranges::all_of(out, [](float sample) { return sample == 0.0f; });
    };

    for (const float level : {0.25f, 0.5f, 0.75f, 1.0f, 1.0f})
    {
        sound->UpdateRain(1.0f);
        CHECK_EQ(sound->RainLevel(), level);
        CHECK(heardAt(level));
    }

    // Easing out to none, and stopped there.
    sound->UpdateRain(0.0f);
    CHECK_EQ(sound->RainLevel(), 0.75f);
    CHECK(heardAt(0.75f));

    for (int32_t frame = 0; frame < 3; ++frame)
    {
        sound->UpdateRain(0.0f);
    }

    CHECK_EQ(sound->RainLevel(), 0.0f);
    CHECK(silent());

    // Outside a scenario Update hears none: it eases the rain out.
    sound->UpdateRain(1.0f);
    sound->Update();
    CHECK_EQ(sound->RainLevel(), 0.0f);

    // Without the pack's loop, none; one put in after isn't looked for till the purge (a mission's end).
    std::filesystem::remove(loop);
    sound->UpdateRain(1.0f);
    CHECK(silent());
    Write(loop, Flac());
    sound->UpdateRain(1.0f);
    CHECK(silent());
    sound->PurgeSoundSystem();
    CHECK_EQ(sound->RainLevel(), 0.0f);
    sound->UpdateRain(1.0f);
    CHECK(heardAt(0.25f));

    // A downpour is heard louder than rain, and a clear sky not at all; nothing is heard while no rain is drawn.
    CHECK(MCWeather::LookOf(MCWeatherKind::Downpour).Heard > MCWeather::LookOf(MCWeatherKind::Rain).Heard);
    CHECK_EQ(MCWeather::LookOf(MCWeatherKind::Clear).Heard, 0.0f);
    const int savedWeather = GHdWeather;
    GHdWeather = HdWeatherDownpour;
    CHECK_EQ(MCWeather::Heard(), 0.0f);
    GHdWeather = savedWeather;
    FrameLength = savedFrame;
}

TEST_CASE("tool: --hd-audio-pack verifies every generated music, effect and voice against the game's reader")
{
    const auto pack = MCTest::Option("hd-audio-pack");
    const auto game = MCTest::Option("game");

    if (!pack.has_value() || !game.has_value())
    {
        return;
    }

    MCHdArt::SetPackFolder(std::filesystem::path(*pack));
    struct Restore
    {
        int Setting = GHdAudio;
        ~Restore()
        {
            GHdAudio = Setting;
            MCHdArt::SetPackFolder({});
            MCHdAudio::Forget();
        }
    } restore;
    GHdAudio = HdAudioOn;
    MCHdAudio::Forget();
    size_t samples = 0;
    size_t music = 0;
    const bool samplesOnly = MCTest::Option("hd-audio-samples-only").has_value();
    const auto checkSample = [&](std::span<const uint8_t> wave)
    {
        const auto original = MCAudio::ParseWave(wave);

        if (!original)
        {
            return;
        }

        CHECK(MCHdAudio::Sample(wave) != nullptr);
        auto decoded =
            MCFlac::Open(std::filesystem::path(*pack) / "audio/samples" / (MCHdAudio::SampleKey(wave) + ".flac"));
        CHECK(decoded != nullptr);

        if (!decoded)
        {
            return;
        }

        CHECK(decoded->Validate());
        CHECK(MCHdAudio::MatchesLength(*decoded, original->Samples.size() / original->Format.BlockAlign(),
                                       original->Format.Rate));
        MCSoundResource resource(wave);
        CHECK_EQ(resource.Format.nSamplesPerSec, uint32_t{48000});
        CHECK_EQ(resource.WaveSize, static_cast<uint32_t>(decoded->Frames() * decoded->Format().BlockAlign()));
        ++samples;
    };

    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(*game) / "data" / "sound"))
    {
        const auto name = entry.path().filename().string();
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        MCTest::Scope scope(name);

        if (extension == ".PAK")
        {
            MCPacketFile file;
            CHECK_EQ(file.Open(entry.path().string()), 0);

            for (int32_t packet = 0; packet < file.GetNumPackets(); ++packet)
            {
                MCTest::Scope packetScope(std::format("packet {}", packet));

                if (file.SeekPacket(packet) != 0 || file.GetPacketSize() == 0)
                {
                    continue;
                }

                std::vector<uint8_t> wave(static_cast<size_t>(file.GetPacketSize()));
                CHECK_EQ(file.ReadPacket(packet, wave), file.GetPacketSize());
                checkSample(wave);
            }
        }
        else if (extension == ".WAV")
        {
            if (name.starts_with("MUSIC"))
            {
                if (samplesOnly)
                {
                    continue;
                }

                const auto path = MCHdAudio::Music(entry.path().string());
                CHECK(path.has_value());
                MCSoundResource stream(MCSoundResourceType::Stream, entry.path().string());
                CHECK_EQ(stream.Format.nSamplesPerSec, uint32_t{48000});
                CHECK_EQ(stream.Format.nChannels, uint16_t{2});
                CHECK_EQ(stream.WaveSize, uint32_t{0});
                ++music;
            }
            else
            {
                std::ifstream file(entry.path(), std::ios::binary);
                std::vector<uint8_t> wave(static_cast<size_t>(entry.file_size()));
                file.read(reinterpret_cast<char*>(wave.data()), static_cast<std::streamsize>(wave.size()));
                checkSample(wave);
            }
        }
    }

    CHECK(samples > 0);
    CHECK(music > 0 || samplesOnly);
    std::cout << "  verified " << music << " music tracks and " << samples << " samples with the engine's PAK reader\n";
}
