#include "stdafx.h"
#include "MCTest.h"
#include "MCTestAudio.h"
#include "TestGame.h"
#include "gameos/MCSoundResource.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "platform/MCFileSystem.h"
#include "sound/MCHdAudio.h"
#include "sound/MCSoundReplacements.h"

namespace
{
    /// <summary>The game's sound folder, as SYSTEM.CFG names it (the tests don't boot, so it isn't read).</summary>
    constexpr std::string_view GameSoundPath = "data\\sound\\";
    /// <summary>SOUND.PAK's plain laser shot, the ER laser's, and its first packet (an empty slot).</summary>
    constexpr int32_t LaserPacket = 1;
    constexpr int32_t ErLaserPacket = 3;
    /// <summary>A replacement's length in frames and rate: neither the laser's.</summary>
    constexpr uint32_t ReplacementFrames = 3000;
    constexpr uint32_t ReplacementRate = 44100;
    /// <summary>The replacements' samples, half of full scale, as 16-bit PCM.</summary>
    constexpr int16_t HalfScale = 0x4000;
    /// <summary>The music replacement: 100 ms at 22050 Hz, mono.</summary>
    constexpr uint32_t MusicFrames = 2205;
    constexpr uint32_t MusicRate = 22050;
    constexpr uint32_t MusicMs = 100;
    /// <summary>The bytes read past the music's end, to see silence or the loop.</summary>
    constexpr size_t PastTheEnd = 100;

    /// <summary>The kinds of sample a test wave holds.</summary>
    enum class MCSampleKind
    {
        Pcm24,
        Float32,
    };

    /// <summary>
    /// A RIFF WAVE of <paramref name="frames"/> frames in <paramref name="channels"/> at <paramref name="rate"/>, every
    /// sample half of full scale, as 24-bit PCM or 32-bit float: kinds the game's own sounds never are.
    /// </summary>
    std::vector<uint8_t> Wave(uint32_t frames, uint16_t channels, uint32_t rate, MCSampleKind kind)
    {
        constexpr uint16_t PcmTag = 1;
        constexpr uint16_t FloatTag = 3;
        constexpr int32_t Half24 = 0x400000;
        constexpr float HalfFloat = 0.5f;
        const uint16_t bytesPerSample = kind == MCSampleKind::Pcm24 ? 3 : 4;
        const uint32_t dataBytes = frames * channels * bytesPerSample;
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
        word(36 + dataBytes, 4);
        tag("WAVEfmt ");
        word(16, 4);
        word(kind == MCSampleKind::Pcm24 ? PcmTag : FloatTag, 2);
        word(channels, 2);
        word(rate, 4);
        word(rate * channels * bytesPerSample, 4);
        word(channels * bytesPerSample, 2);
        word(bytesPerSample * 8, 2);
        tag("data");
        word(dataBytes, 4);

        for (uint32_t sample = 0; sample < frames * channels; ++sample)
        {
            word(kind == MCSampleKind::Pcm24 ? static_cast<uint32_t>(Half24) : std::bit_cast<uint32_t>(HalfFloat),
                 bytesPerSample);
        }

        return wave;
    }

    void Write(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    std::string Text(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    /// <summary>SOUND.PAK's packet <paramref name="packet"/>, a wave image (empty for an empty slot).</summary>
    std::vector<uint8_t> SoundPacket(int32_t packet)
    {
        MCPacketFile file;

        if (file.Open(GamePath(SoundPath, "sound", ".pak")) != 0 || file.SeekPacket(packet) != 0 ||
            file.GetPacketSize() <= 0)
        {
            return {};
        }

        std::vector<uint8_t> wave(static_cast<size_t>(file.GetPacketSize()));
        return file.ReadPacket(packet, wave) == file.GetPacketSize() ? wave : std::vector<uint8_t>{};
    }

    /// <summary>The first sample of a resource's samples, as signed 16-bit.</summary>
    int16_t FirstSample(std::span<const uint8_t> pcm)
    {
        return pcm.size() < 2 ? int16_t{0} : static_cast<int16_t>(pcm[0] | (pcm[1] << 8));
    }

    /// <summary>
    /// A user folder of the test's own with the game's sound paths set and HD audio off; all put back after, the
    /// replacements forgotten.
    /// </summary>
    struct Fixture
    {
        std::filesystem::path User =
            std::filesystem::temp_directory_path() /
            std::format("mc_tests_sound_replacements_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::path Folder = User / MCSoundReplacements::FolderName;
        std::string PreviousSoundPath = SoundPath;
        std::string PreviousCdSoundPath = CDsoundPath;
        int PreviousHdAudio = GHdAudio;

        Fixture()
        {
            std::filesystem::create_directories(User);
            MCFileSystem::SetUserRoot(User);
            SoundPath = GameSoundPath;
            CDsoundPath = GameSoundPath;
            GHdAudio = HdAudioOff;
            MCSoundReplacements::Forget();
        }

        ~Fixture()
        {
            MCSoundReplacements::Forget();
            GHdAudio = PreviousHdAudio;
            SoundPath = PreviousSoundPath;
            CDsoundPath = PreviousCdSoundPath;
            MCFileSystem::SetUserRoot({});
            std::error_code error;
            std::filesystem::remove_all(User, error);
        }

        std::string Heard() const { return Text(Folder / MCSoundReplacements::HeardFile); }
    };
}

TEST_CASE_ISOLATED("game: a sound replacement of any length and kind plays in place of a PAK sound, and is listed")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Fixture fixture;
    const std::vector<uint8_t> laser = SoundPacket(LaserPacket);
    const std::vector<uint8_t> erLaser = SoundPacket(ErLaserPacket);
    REQUIRE(!laser.empty());
    REQUIRE(!erLaser.empty());
    const uint32_t laserSize = MCSoundResource(laser).WaveSize;
    const uint32_t erLaserSize = MCSoundResource(erLaser).WaveSize;

    // No folder: nothing is replaced or listed.
    MCSoundReplacements::Look();
    CHECK(!MCSoundReplacements::Active());
    CHECK_EQ(MCSoundResource(laser).WaveSize, laserSize);

    std::filesystem::create_directories(fixture.Folder);
    Write(fixture.Folder / "SOUND-001.wav", Wave(ReplacementFrames, 2, ReplacementRate, MCSampleKind::Pcm24));
    Write(fixture.Folder / "sound-000.WAV", Wave(ReplacementFrames, 2, ReplacementRate, MCSampleKind::Pcm24));
    Write(fixture.Folder / "SOUND-999.wav", Wave(ReplacementFrames, 2, ReplacementRate, MCSampleKind::Pcm24));
    MCSoundReplacements::Look();
    CHECK(MCSoundReplacements::Active());

    // The laser plays the file: its rate, channels and length, decoded to 16 bits, with HD audio off.
    const MCSoundResource replaced(laser);
    CHECK_EQ(replaced.Format.nSamplesPerSec, ReplacementRate);
    CHECK_EQ(replaced.Format.nChannels, uint16_t{2});
    CHECK_EQ(replaced.Format.wBitsPerSample, uint16_t{16});
    CHECK_EQ(replaced.WaveSize, ReplacementFrames * 2 * sizeof(int16_t));
    CHECK_EQ(FirstSample(replaced.WaveData()), HalfScale);
    // The ER laser has no file: the original.
    CHECK_EQ(MCSoundResource(erLaser).WaveSize, erLaserSize);

    const std::string heard = fixture.Heard();
    CHECK(heard.contains("SOUND-001"));
    CHECK(heard.contains("replaced by SOUND-001.wav"));
    CHECK(heard.contains("  SOUND-003\n"));
    CHECK(heard.contains("! sound-000.WAV is for an empty slot"));
    CHECK(heard.contains("! SOUND-999.wav: no sound has this name"));

    // A FLAC, and a file that isn't a sound (the original plays, and the list says why).
    MCSoundReplacements::Forget();
    std::filesystem::remove(fixture.Folder / "SOUND-001.wav");
    Write(fixture.Folder / "SOUND-003.flac", MCTestAudio::Flac());
    const std::string_view notASound = "not a sound";
    Write(fixture.Folder / "SOUND-001.wav",
          std::span(reinterpret_cast<const uint8_t*>(notASound.data()), notASound.size()));
    MCSoundReplacements::Look();
    const MCSoundResource flac(erLaser);
    CHECK_EQ(flac.Format.nSamplesPerSec, uint32_t{48000});
    CHECK_EQ(flac.WaveSize, uint32_t{960 * 4});
    CHECK_EQ(MCSoundResource(laser).WaveSize, laserSize);
    CHECK(fixture.Heard().contains("! SOUND-001.wav can't be used: it isn't a WAV SDL reads"));

    // The folder gone: the originals again.
    std::filesystem::remove_all(fixture.Folder);
    MCSoundReplacements::Look();
    CHECK(!MCSoundReplacements::Active());
    CHECK_EQ(MCSoundResource(erLaser).WaveSize, erLaserSize);
}

TEST_CASE_ISOLATED("game: a music replacement of any length streams in place of the track, to its own end or loop")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    Fixture fixture;
    std::filesystem::create_directories(fixture.Folder);
    Write(fixture.Folder / "MUSIC14.wav", Wave(MusicFrames, 1, MusicRate, MCSampleKind::Float32));
    MCSoundReplacements::Look();
    MCSoundResource stream(MCSoundResourceType::Stream, GamePath(SoundPath, "MUSIC14", ".WAV"));
    CHECK_EQ(stream.Format.nSamplesPerSec, MusicRate);
    CHECK_EQ(stream.Format.nChannels, uint16_t{1});
    CHECK_EQ(stream.Format.wBitsPerSample, uint16_t{16});
    CHECK_EQ(stream.DurationMs, MusicMs);
    CHECK(fixture.Heard().contains("MUSIC14  replaced by MUSIC14.wav"));

    const size_t bytes = MusicFrames * sizeof(int16_t);
    std::vector<uint8_t> buffer(bytes + PastTheEnd);
    CHECK_EQ(stream.Read(buffer, false), static_cast<uint32_t>(buffer.size()));
    CHECK_EQ(FirstSample(buffer), HalfScale);
    CHECK_EQ(FirstSample(std::span(buffer).subspan(bytes - sizeof(int16_t))), HalfScale);
    CHECK(std::ranges::all_of(std::span(buffer).subspan(bytes), [](uint8_t b) { return b == 0; }));

    // Looping: past the end, the track again from its start.
    stream.Rewind();
    CHECK_EQ(stream.Read(buffer, true), static_cast<uint32_t>(buffer.size()));
    CHECK_EQ(FirstSample(std::span(buffer).subspan(bytes)), HalfScale);

    // Another track has no file: the original's stream.
    MCSoundResource original(MCSoundResourceType::Stream, GamePath(SoundPath, "MUSIC15", ".WAV"));
    CHECK(original.DurationMs > MusicMs);
    CHECK(fixture.Heard().contains("  MUSIC15\n"));
}
