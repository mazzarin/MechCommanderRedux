#include "stdafx.h"
#include "MCMock.h"
#include "MCTest.h"
#include "camera/MCCameraList.h"
#include "fakes/MCNullAudioDevice.h"
#include "gameos/MCSoundChannel.h"
#include "gameos/MCSoundRenderer.h"
#include "main/MCGameContext.h"
#include "platform/MCAudio.h"
#include "platform/MCFileSystem.h"
#include "platform/MCServices.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "object/MCMechWarrior.h"
#include "sound/MCRadio.h"
#include "sound/MCRadioMessage.h"
#include "sound/MCSoundSystem.h"

// The sound system (sound/) and the sound renderer under it (gameos/): effect channel allocation, the radio queue
// and its playback, and the pan rule, on the null audio device.

namespace
{
    /// <summary>A RIFF WAVE image: 22050 Hz, mono, 8-bit, <paramref name="samples"/> samples of silence.</summary>
    std::vector<uint8_t> Wave(uint32_t samples)
    {
        std::vector<uint8_t> wave;
        const auto put32 = [&](uint32_t value)
        {
            for (int i = 0; i < 4; i++)
            {
                wave.push_back(static_cast<uint8_t>(value >> (i * 8)));
            }
        };

        const auto put16 = [&](uint16_t value)
        {
            wave.push_back(static_cast<uint8_t>(value));
            wave.push_back(static_cast<uint8_t>(value >> 8));
        };

        const auto putTag = [&](std::string_view tag) { wave.insert(wave.end(), tag.begin(), tag.end()); };
        putTag("RIFF");
        put32(36 + samples);
        putTag("WAVE");
        putTag("fmt ");
        put32(16);
        put16(1);
        put16(1);
        put32(22050);
        put32(22050);
        put16(1);
        put16(8);
        putTag("data");
        put32(samples);
        wave.resize(wave.size() + samples, 0x80);
        return wave;
    }

    /// <summary>
    /// A sound system on the null audio device with <paramref name="bites"/> loaded sound bites (volume 1), effects
    /// heard up to 100 units away.
    /// </summary>
    struct MCSoundFixture
    {
        explicit MCSoundFixture(uint32_t bites = 16)
        {
            scope.Context().SetAudio(std::make_unique<MCNullAudioDevice>());
            scope.Context().SetSoundSystem(std::make_unique<MCSoundSystem>());
            sound = SoundSystem();
            renderer = SoundRenderer();
            sound->MaxSoundDistance = 100.0f;
            sound->Sounds.resize(bites);

            for (MCSoundBite& bite : sound->Sounds)
            {
                const std::vector<uint8_t> wave = Wave(64);
                bite.BiteData = wave;
                bite.Volume = 1.0f;
            }
        }

        /// <summary>The null audio device the fixture installed.</summary>
        MCNullAudioDevice& Device() { return dynamic_cast<MCNullAudioDevice&>(scope.Context().Audio()); }

        MCTestContextScope scope;
        std::shared_ptr<MCSoundSystem> sound;
        std::shared_ptr<MCSoundRenderer> renderer;
    };

    /// <summary>A pilot for the queue, which only compares pilots.</summary>
    std::shared_ptr<MCMechWarrior> NewPilot()
    {
        return std::make_shared<MCMechWarrior>();
    }

    /// <summary>A message of <paramref name="pilot"/> with <paramref name="priority"/> and packet <paramref
    /// name="msgId"/>.</summary>
    std::unique_ptr<MCRadioMessage> Message(std::shared_ptr<MCMechWarrior> pilot, uint8_t priority, uint32_t msgId,
                                            MCRadioMessageType type = MCRadioMessageType::MoveTo)
    {
        auto message = std::make_unique<MCRadioMessage>();
        message->Pilot = pilot;
        message->Priority = priority;
        message->MsgId = msgId;
        message->MsgType = type;
        return message;
    }

    /// <summary>The packets waiting in <paramref name="queue"/>, first to play first.</summary>
    std::vector<uint32_t> Waiting(const MCRadioQueue& queue)
    {
        std::vector<uint32_t> ids;

        for (size_t i = 0; i < queue.Size(); i++)
        {
            ids.push_back(queue.At(i).MsgId);
        }

        return ids;
    }
}

/// <summary>
/// Effects at a place take the first free channel of 1..9 and effects at the camera 11..13; when those are all busy
/// an effect isn't played (nothing is cut off), and an effect already playing isn't started twice.
/// </summary>
TEST_CASE("sound: effect channels are allocated in order and a full set refuses")
{
    MCSoundFixture fixture;
    MCSoundSystem& sound = *fixture.sound;

    for (uint32_t sample = 0; sample < 9; sample++)
    {
        MCTest::Scope entry(std::format("sample {}", sample));
        CHECK_EQ(sound.PlayDigitalSample(sample, 1, std::nullopt, false, false), static_cast<int32_t>(sample) + 1);
    }

    CHECK_EQ(sound.PlayDigitalSample(9, 1, std::nullopt, false, false), -1);
    CHECK_CALLED(fixture.Device().Played, 9);
    // Already playing: refused, even at the camera.
    CHECK_EQ(sound.PlayDigitalSample(3, 1, std::nullopt, true, false), -1);

    for (uint32_t sample = 10; sample < 13; sample++)
    {
        MCTest::Scope entry(std::format("camera sample {}", sample));
        CHECK_EQ(sound.PlayDigitalSample(sample, 1, std::nullopt, true, false), static_cast<int32_t>(sample) + 1);
    }

    CHECK_EQ(sound.PlayDigitalSample(13, 1, std::nullopt, true, false), -1);
    CHECK(sound.IsSamplePlaying(12));
    CHECK(!sound.IsSamplePlaying(13));
    // A bite the sound file doesn't have.
    CHECK_EQ(sound.PlayDigitalSample(99, 1, std::nullopt, false, false), -1);
}

/// <summary>
/// A stopped effect fades: each update lowers its channel 1/64, and once silent the channel stops, frees its wave and
/// takes the next effect.
/// </summary>
TEST_CASE("sound: a stopped effect fades out over updates and frees its channel")
{
    MCSoundFixture fixture;
    MCSoundSystem& sound = *fixture.sound;
    MCSoundRenderer& renderer = *fixture.renderer;
    sound.DigitalMasterVolume = 32;
    REQUIRE_EQ(sound.PlayDigitalSample(0, 1, std::nullopt, false, false), 1);
    // The volume is the effects volume (of 128); the bite's is full.
    CHECK_EQ(renderer.ChannelVolume(1), 0.25f);
    // Centred: the sound is at the listener.
    CHECK_EQ(renderer.ChannelResource(1) != nullptr, true);

    sound.StopDigitalSample(1);
    CHECK(sound.SampleChannels[1].FadeDown);
    CHECK(!sound.IsSamplePlaying(0));
    sound.Update();
    CHECK_EQ(renderer.ChannelVolume(1), 0.25f - 1.0f / 64.0f);
    CHECK(renderer.IsPlaying(1));

    for (int32_t frame = 0; frame < 15; frame++)
    {
        sound.Update();
    }

    CHECK(!renderer.IsPlaying(1));
    CHECK(renderer.ChannelResource(1) == nullptr);
    CHECK(!sound.SampleChannels[1].FadeDown);
    CHECK_EQ(sound.PlayDigitalSample(5, 1, std::nullopt, false, false), 1);
}

/// <summary>
/// An interface sound plays as an effect without a source does (the first free channel of 1..9), at the interface
/// volume instead of the effects volume: at 0 it is silent while the effects still play.
/// </summary>
TEST_CASE("sound: an interface sound plays at the interface volume, an effect at the effects volume")
{
    MCSoundFixture fixture;
    MCSoundSystem& sound = *fixture.sound;
    MCSoundRenderer& renderer = *fixture.renderer;
    sound.DigitalMasterVolume = 64;
    sound.InterfaceLevel = 32;
    sound.Sounds[1].Volume = 0.5f;
    REQUIRE_EQ(sound.PlayInterfaceSample(0), 1);
    CHECK_EQ(renderer.ChannelVolume(1), 0.25f);
    REQUIRE_EQ(sound.PlayDigitalSample(2, 1, std::nullopt, false, false), 2);
    CHECK_EQ(renderer.ChannelVolume(2), 0.5f);
    // The bite's own volume still lowers it: 0.5 is 10 dB under the interface volume's -24 dB.
    REQUIRE_EQ(sound.PlayInterfaceSample(1), 3);
    CHECK(std::abs(MCSoundChannel::BufferVolume(renderer.ChannelVolume(3)) - (-3408)) <= 2);
    // Already playing: refused, as an effect is.
    CHECK_EQ(sound.PlayInterfaceSample(2), -1);

    sound.InterfaceLevel = 0;
    REQUIRE_EQ(sound.PlayInterfaceSample(3), 4);
    CHECK_EQ(renderer.ChannelVolume(4), 0.0f);
    REQUIRE_EQ(sound.PlayDigitalSample(4, 1, std::nullopt, false, false), 5);
    CHECK_EQ(renderer.ChannelVolume(5), 0.5f);
}

/// <summary>
/// A channel volume becomes a fader taper (the gain is the volume squared): full volume is 0 dB, half is -12 dB, a
/// slider's first step (1 of 127) is nearly silent and 0 is silence (OB-176; the original stopped at -20 dB).
/// </summary>
TEST_CASE("sound: the channel volume tapers to silence at 0 (OB-176)")
{
    CHECK_EQ(MCSoundChannel::BufferVolume(1.0f), MCSoundBuffer::VolumeMax);
    CHECK_EQ(MCSoundChannel::BufferVolume(0.5f), -1204);
    CHECK_EQ(MCSoundChannel::BufferVolume(0.1f), -4000);
    CHECK_EQ(MCSoundChannel::BufferVolume(1.0f / 127.0f), -8415);
    CHECK_EQ(MCSoundChannel::BufferVolume(0.0f), MCSoundBuffer::VolumeMin);
    CHECK_EQ(MCAudio::GainFromHundredthsDb(MCSoundChannel::BufferVolume(0.0f)), 0.0f);
    // Out of range: a fade's overshoot.
    CHECK_EQ(MCSoundChannel::BufferVolume(-0.5f), MCSoundBuffer::VolumeMin);
    CHECK_EQ(MCSoundChannel::BufferVolume(1.5f), MCSoundBuffer::VolumeMax);
    CHECK_EQ(MCSoundChannel::BufferVolume(1.0e-4f), MCSoundBuffer::VolumeMin);

    // Every slider step is quieter than the one above it.
    for (int32_t step = 1; step <= 127; step++)
    {
        MCTest::Scope entry(std::format("step {}", step));
        CHECK(MCSoundChannel::BufferVolume((step - 1) / 128.0f) < MCSoundChannel::BufferVolume(step / 128.0f));
    }
}

/// <summary>
/// A sound bite's volume (SOUND.SND) is the level the original gave it, 20 * (volume - 1) dB under the effects volume,
/// at any effects volume: at 64 (-12 dB) a 1.0 autocannon is -12 dB, a 0.25 laser -27 dB and a 0.15 missile -29 dB
/// (issue #31: through the fader taper the laser fell to -36 dB and the missile to -45 dB).
/// </summary>
TEST_CASE("sound: a bite's volume lowers it by its original decibels at any effects volume")
{
    MCSoundFixture fixture;
    MCSoundSystem& sound = *fixture.sound;
    MCSoundRenderer& renderer = *fixture.renderer;
    const auto hundredthsDb = [&](int32_t channel)
    { return MCSoundChannel::BufferVolume(renderer.ChannelVolume(channel)); };
    const std::array<float, 5> volumes{1.0f, 0.75f, 0.25f, 0.15f, 0.0f};

    for (size_t i = 0; i < volumes.size(); i++)
    {
        sound.Sounds[i].Volume = volumes[i];
    }

    for (const uint8_t level : {uint8_t{64}, uint8_t{127}})
    {
        MCTest::Scope entry(std::format("effects volume {}", level));
        sound.DigitalMasterVolume = level;
        const int32_t full = MCSoundChannel::BufferVolume(level / 128.0f);

        for (size_t i = 0; i < volumes.size(); i++)
        {
            MCTest::Scope bite(std::format("bite volume {}", volumes[i]));
            REQUIRE_EQ(sound.PlayDigitalSample(static_cast<uint32_t>(i), 1, std::nullopt, false, false),
                       static_cast<int32_t>(i) + 1);
            const int32_t expected = full + std::lround(2000.0f * (volumes[i] - 1.0f));
            CHECK(std::abs(hundredthsDb(static_cast<int32_t>(i) + 1) - expected) <= 2);
        }

        // Stopped channels are free again after an update.
        for (int32_t channel = 1; channel <= static_cast<int32_t>(volumes.size()); channel++)
        {
            renderer.Stop(channel);
        }

        sound.Update();
    }

    // At 64: the values the issue is about.
    CHECK_EQ(MCSoundChannel::BufferVolume(64 / 128.0f * SoundBiteGain(1.0f)), -1204);
    CHECK(std::abs(MCSoundChannel::BufferVolume(64 / 128.0f * SoundBiteGain(0.25f)) - (-2704)) <= 2);
    CHECK(std::abs(MCSoundChannel::BufferVolume(64 / 128.0f * SoundBiteGain(0.15f)) - (-2904)) <= 2);
    // Out of range volumes clamp.
    CHECK_EQ(SoundBiteGain(2.0f), 1.0f);
    CHECK_EQ(SoundBiteGain(-1.0f), SoundBiteGain(0.0f));
}

/// <summary>
/// While something burns on screen, the fire loop (bite 22, "Building on Fire", volume 0.25) plays centred on the
/// noise channel at its own volume under the effects volume: -27 dB at 64 (OB-181: the original played it at the
/// effects volume, as loud as a 1.0 bite).
/// </summary>
TEST_CASE("sound: the fire loop plays at its bite's volume (OB-181)")
{
    MCSoundFixture fixture(32);
    MCSoundSystem& sound = *fixture.sound;
    MCSoundRenderer& renderer = *fixture.renderer;
    fixture.scope.Context().SetScenario(std::make_unique<MCScenario>());
    fixture.scope.Context().Scenario()->MusicPending = true;
    const auto cameras = std::make_shared<MCCameraList>();
    fixture.scope.Context().SetCameraList(cameras);
    const auto camera = std::make_shared<MCCamera>();
    cameras->SetEye(camera);
    sound.DigitalMasterVolume = 64;
    sound.Sounds[0x16].Volume = 0.25f;
    const int32_t saved = SomethingOnFire;
    SomethingOnFire = 1;
    sound.Update();
    SomethingOnFire = saved;
    REQUIRE(renderer.IsPlaying(NOISE_CHANNEL));
    CHECK_EQ(sound.SampleChannels[NOISE_CHANNEL].SampleId, 0x16);
    CHECK(std::abs(MCSoundChannel::BufferVolume(renderer.ChannelVolume(NOISE_CHANNEL)) - (-2704)) <= 2);
}

/// <summary>
/// The pan is the angle from the screen's up direction (the world turned 45 degrees: up is +x -y): straight ahead
/// and straight behind are centred (64), a quarter turn right is (nearly) full right (128), left full left (0), and
/// half way is half way.
/// </summary>
TEST_CASE("sound: the pan follows the angle from the screen's up direction")
{
    CHECK_EQ(SoundPanPosition(1.0f, -1.0f), 64);
    // A quarter turn: the float angle falls a hair short of 90 degrees, a step short of the edge.
    CHECK(SoundPanPosition(1.0f, 1.0f) >= 127);
    CHECK(SoundPanPosition(-1.0f, -1.0f) <= 1);
    CHECK_EQ(SoundPanPosition(-1.0f, 1.0f), 64);
    CHECK_EQ(SoundPanPosition(1.0f, 0.0f), 96);
    CHECK_EQ(SoundPanPosition(0.0f, -1.0f), 32);
}

/// <summary>Messages wait by priority (lower first), equal priorities in the order they came.</summary>
TEST_CASE("sound: the radio queue orders messages by priority")
{
    const std::shared_ptr<MCMechWarrior> a = NewPilot();
    MCRadioQueue queue;
    CHECK(queue.Push(Message(a, 4, 1)) == nullptr);
    CHECK(queue.Push(Message(a, 2, 2)) == nullptr);
    CHECK(queue.Push(Message(a, 4, 3)) == nullptr);
    CHECK(queue.Push(Message(a, 3, 4)) == nullptr);
    CHECK(queue.Push(Message(a, 2, 5)) == nullptr);
    CHECK((Waiting(queue) == std::vector<uint32_t>{2, 5, 4, 1, 3}));
    CHECK_EQ(queue.PopFront()->MsgId, 2u);
    CHECK_EQ(queue.Size(), 4u);
}

/// <summary>
/// A pilot can't queue a message while one of his of a higher priority waits, and nobody can queue a type while a
/// message of that type with priority 2 or more waits.
/// </summary>
TEST_CASE("sound: the radio queue refuses a pilot's lower messages and repeated types")
{
    const std::shared_ptr<MCMechWarrior> a = NewPilot();
    const std::shared_ptr<MCMechWarrior> b = NewPilot();
    MCRadioQueue queue;
    queue.Push(Message(a, 2, 1, MCRadioMessageType::UnderAttack));
    CHECK(!queue.MayQueue(*a, 3, MCRadioMessageType::Taunt));
    CHECK(queue.MayQueue(*a, 2, MCRadioMessageType::Taunt));
    CHECK(queue.MayQueue(*b, 4, MCRadioMessageType::Taunt));
    CHECK(!queue.MayQueue(*b, 1, MCRadioMessageType::UnderAttack));

    MCRadioQueue urgent;
    urgent.Push(Message(a, 1, 1, MCRadioMessageType::Death));
    // A priority 1 message doesn't block its type.
    CHECK(urgent.MayQueue(*b, 1, MCRadioMessageType::Death));
}

/// <summary>
/// A full queue (8) takes no message that would go last. Original behaviour (OB-061): putting a message ahead of 7
/// or 8 waiting ones leaves 7, the lowest ones dropped.
/// </summary>
TEST_CASE("sound: the radio queue holds 8 and drops the lowest on an early insert (OB-061)")
{
    const std::shared_ptr<MCMechWarrior> a = NewPilot();
    MCRadioQueue queue;

    for (uint32_t id = 1; id <= 7; id++)
    {
        queue.Push(Message(a, 4, id));
    }

    // At the end: the eighth fits.
    CHECK(queue.Push(Message(a, 4, 8)) == nullptr);
    CHECK_EQ(queue.Size(), 8u);
    std::unique_ptr<MCRadioMessage> refused = queue.Push(Message(a, 4, 9));
    REQUIRE(refused != nullptr);
    CHECK_EQ(refused->MsgId, 9u);

    // Ahead of 8: two go.
    CHECK(queue.Push(Message(a, 2, 10)) == nullptr);
    CHECK((Waiting(queue) == std::vector<uint32_t>{10, 1, 2, 3, 4, 5, 6}));

    // Ahead of 7: one goes, although there was room.
    CHECK(queue.Push(Message(a, 3, 11)) == nullptr);
    CHECK((Waiting(queue) == std::vector<uint32_t>{10, 11, 1, 2, 3, 4, 5}));
}

/// <summary>
/// Queueing drops a waiting copy (same turn, same packet); a priority 1 message cuts off the message playing and the
/// pilot's waiting ones.
/// </summary>
TEST_CASE("sound: a priority 1 radio message cuts off its pilot's other messages")
{
    MCSoundFixture fixture;
    MCSoundSystem& sound = *fixture.sound;
    const std::shared_ptr<MCMechWarrior> a = NewPilot();
    const std::shared_ptr<MCMechWarrior> b = NewPilot();
    CHECK(sound.QueueRadioMessage(Message(a, 3, 1)));
    CHECK(sound.QueueRadioMessage(Message(b, 3, 2)));
    CHECK(sound.QueueRadioMessage(Message(a, 4, 3)));
    CHECK(sound.QueueRadioMessage(Message(b, 3, 2)));
    CHECK((Waiting(sound.RadioQueue) == std::vector<uint32_t>{1, 2, 3}));

    sound.CurrentMessage = Message(b, 3, 7);
    CHECK(sound.QueueRadioMessage(Message(a, 1, 4, MCRadioMessageType::Death)));
    CHECK(sound.CurrentMessage == nullptr);
    CHECK(sound.WholeMsgDone);
    CHECK((Waiting(sound.RadioQueue) == std::vector<uint32_t>{4, 2}));
}

/// <summary>
/// The radio plays a message's static, then each speech fragment in turn, each once the speech channel has stopped;
/// after the last, the message goes and the next one starts.
/// </summary>
TEST_CASE("sound: a radio message plays its static, then its fragments")
{
    MCSoundFixture fixture;
    MCSoundSystem& sound = *fixture.sound;
    MCSoundRenderer& renderer = *fixture.renderer;
    sound.WholeMsgDone = true;
    const std::shared_ptr<MCMechWarrior> a = NewPilot();
    std::unique_ptr<MCRadioMessage> message = Message(a, 3, 1);
    message->Noise = Wave(100);
    message->Fragments.push_back(Wave(200));
    message->Fragments.push_back(Wave(300));
    REQUIRE(sound.QueueRadioMessage(std::move(message)));

    // Each step: the speech channel plays this many samples, then (here) ends.
    for (uint32_t samples : {100u, 200u, 300u})
    {
        MCTest::Scope entry(std::format("{} samples", samples));
        sound.Update();
        REQUIRE(renderer.ChannelResource(PILOT_SPEECH_CHANNEL) != nullptr);
        CHECK_EQ(renderer.ChannelResource(PILOT_SPEECH_CHANNEL)->WaveSize, samples);
        CHECK(renderer.IsPlaying(PILOT_SPEECH_CHANNEL));
        // Nothing moves on while it plays.
        sound.Update();
        CHECK_EQ(renderer.ChannelResource(PILOT_SPEECH_CHANNEL)->WaveSize, samples);
        renderer.Stop(PILOT_SPEECH_CHANNEL);
    }

    CHECK_CALLED(fixture.Device().Played, 3);
    REQUIRE(sound.CurrentMessage != nullptr);
    sound.Update();
    CHECK(sound.WholeMsgDone);
    sound.Update();
    CHECK(sound.CurrentMessage == nullptr);
    CHECK(renderer.ChannelResource(PILOT_SPEECH_CHANNEL) == nullptr);
}

/// <summary>The purge stops everything, empties the queue and frees the loaded bites and the radios.</summary>
TEST_CASE("sound: the purge stops the channels and drops the radio messages")
{
    MCSoundFixture fixture;
    MCSoundSystem& sound = *fixture.sound;
    const std::shared_ptr<MCMechWarrior> a = NewPilot();
    REQUIRE_EQ(sound.PlayDigitalSample(0, 1, std::nullopt, false, false), 1);
    sound.QueueRadioMessage(Message(a, 3, 1));
    sound.PurgeSoundSystem();
    CHECK(!fixture.renderer->IsPlaying(1));
    CHECK(fixture.renderer->ChannelResource(1) == nullptr);
    CHECK_EQ(fixture.renderer->ResourceCount(), 0u);
    CHECK_EQ(sound.RadioQueue.Size(), 0u);
    CHECK(sound.Sounds[0].BiteData.empty());
    CHECK_EQ(sound.RadioCount(), 0u);
}

/// <summary>
/// On a computer without a playback device, a sound still ends after its length, as it would on one: the operation
/// tab waits for Betty's line to end (MCBriefingScreen::SetUpOperation), and without a device it waited for good.
/// SDL's disk driver with nowhere to write stands in for the computer: its default device can't be opened.
/// </summary>
TEST_CASE_ISOLATED("sound: without a playback device Betty's line still ends, as the operation tab waits for it")
{
    // A tenth of a second at the test wave's 22050 Hz.
    constexpr uint32_t lineSamples = 2205;
    constexpr auto lineLength = std::chrono::milliseconds(100);
    constexpr auto waitLimit = std::chrono::seconds(3);
    constexpr auto waitStep = std::chrono::milliseconds(2);

    const std::filesystem::path nowhere = std::filesystem::temp_directory_path() / "mc_tests no folder" / "sound.raw";
    REQUIRE(!std::filesystem::exists(nowhere.parent_path()));
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
    SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, MCFileSystem::ToUtf8(nowhere).c_str());
    REQUIRE(!MCAudio::Open().has_value());

    {
        MCTestContextScope scope;
        scope.Context().SetAudio(std::make_unique<MCSdlAudioDevice>());
        scope.Context().SetSoundSystem(std::make_unique<MCSoundSystem>());
        MCSoundSystem& sound = *SoundSystem();
        MCSoundRenderer& renderer = *SoundRenderer();

        const std::vector<uint8_t> line = Wave(lineSamples);
        renderer.Play(BETTY_CHANNEL, renderer.CreateResource(line));
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(sound.IsChannelPlaying(BETTY_CHANNEL));

        while (sound.IsChannelPlaying(BETTY_CHANNEL) && std::chrono::steady_clock::now() - start < waitLimit)
        {
            std::this_thread::sleep_for(waitStep);
        }

        const auto took = std::chrono::steady_clock::now() - start;
        CHECK(!sound.IsChannelPlaying(BETTY_CHANNEL));
        // Played at a device's rate, not dropped at once.
        CHECK(took >= lineLength / 2);
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
    SDL_ResetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE);
}

/// <summary>
/// radio.csv rows as the retail file has them: blank style columns (a space) read 0, fields split at commas, 'y'
/// lets the pilot say who he is.
/// </summary>
TEST_CASE("sound: radio.csv rows parse as the game reads them")
{
    const MCRadioMessageInfo moveTo = ParseRadioMessageRow("RADIO_MOVETO,4,3,x,3,70,20,10,n,0");
    CHECK_EQ(moveTo.Priority, 4);
    CHECK_EQ(moveTo.ShelfLife, 3.0f);
    CHECK_EQ(moveTo.MovieCode, 'x');
    CHECK_EQ(moveTo.Styles, 3);
    CHECK((moveTo.StyleChance == std::array<uint8_t, 3>{70, 20, 10}));
    CHECK(!moveTo.PilotIdentifiesSelf);
    CHECK_EQ(moveTo.MsgId, 0);

    const MCRadioMessageInfo captured = ParseRadioMessageRow("RADIO_CAPTURED_BUILDING,2,8,A,1, , , ,y,12\r\n");
    CHECK_EQ(captured.Priority, 2);
    CHECK_EQ(captured.ShelfLife, 8.0f);
    CHECK_EQ(captured.MovieCode, 'A');
    CHECK_EQ(captured.Styles, 1);
    CHECK((captured.StyleChance == std::array<uint8_t, 3>{0, 0, 0}));
    CHECK(captured.PilotIdentifiesSelf);
    CHECK_EQ(captured.MsgId, 12);

    // A short row: the missing fields take their defaults.
    const MCRadioMessageInfo shortRow = ParseRadioMessageRow("RADIO_X");
    CHECK_EQ(shortRow.Priority, 4);
    CHECK_EQ(shortRow.Styles, 1);
    CHECK_EQ(shortRow.MsgId, 0);
}
