#include "stdafx.h"
#include "sound/MCHdAudio.h"
#include "lib/MCFitIniFile.h"
#include "platform/MCFlac.h"
#include "sprite/MCHdArt.h"

int GHdAudio = 0;

namespace
{
    /// <summary>The audio.cfg format this code reads.</summary>
    constexpr int32_t PackFormat = 1;
    /// <summary>The decoded samples kept in memory before the least recently used go.</summary>
    constexpr size_t SampleCacheBudget = 64u * 1024u * 1024u;
    /// <summary>The most one replacement may decode to, whatever a corrupt header claims.</summary>
    constexpr uint64_t MaxSampleBytes = 32u * 1024u * 1024u;
    /// <summary>The music names the pack may replace: MUSIC and two digits.</summary>
    constexpr std::string_view MusicPrefix = "MUSIC";
    constexpr size_t MusicNameLength = 7;

    /// <summary>What was found in the pack in use: whether it is one, and the samples looked up so far.</summary>
    struct State
    {
        /// <summary>The pack version (MCHdArt::PackVersion) the rest was found for.</summary>
        std::optional<uint64_t> Version;
        /// <summary>Whether the pack folder has an audio.cfg of a format this code reads.</summary>
        bool Readable = false;
        /// <summary>Whether that pack has orchestral music.</summary>
        bool Orchestral = false;
        /// <summary>The replacements by key (null: the wave has none), and their keys, most recently used first.</summary>
        std::unordered_map<std::string,
                           std::pair<std::shared_ptr<const MCHdAudio::Decoded>, std::list<std::string>::iterator>>
            Samples;
        std::list<std::string> Order;
        size_t Bytes = 0;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    bool ReadsPack()
    {
        const std::filesystem::path path = MCHdArt::PackFolder() / "audio" / "audio.cfg";
        std::error_code error;

        if (!std::filesystem::is_regular_file(path, error))
        {
            return false;
        }

        MCFitIniFile file;
        return file.Open(path.string()) == 0 && file.SeekBlock("HDAudioPack") == 0 &&
               file.Read<int32_t>("Format") == PackFormat;
    }

    /// <summary>The state for the pack in use, found again when the pack (or whether there is one) may have changed.</summary>
    State& Current()
    {
        State& state = TheState();

        if (const uint64_t version = MCHdArt::PackVersion(); state.Version != version)
        {
            state.Samples.clear();
            state.Order.clear();
            state.Bytes = 0;
            state.Version = version;
            state.Readable = ReadsPack();
            std::error_code error;
            state.Orchestral = state.Readable && std::filesystem::is_directory(
                                                     MCHdArt::PackFolder() / "audio" / "music" / "orchestral", error);
        }

        return state;
    }

    /// <summary>Whether the pack's audio plays: HD audio is on and the pack folder has a readable audio.cfg.</summary>
    bool Active()
    {
        return GHdAudio != HdAudioOff && Current().Readable;
    }

    std::optional<std::filesystem::path> Existing(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) ? std::optional(path) : std::nullopt;
    }

    std::string KeyOf(const MCWaveData& parsed)
    {
        uint64_t hash = 14695981039346656037ull;
        const auto byte = [&hash](uint8_t value) { hash = (hash ^ value) * 1099511628211ull; };

        for (unsigned i = 0; i < 4; ++i)
        {
            byte(static_cast<uint8_t>(parsed.Format.Rate >> (i * 8)));
        }

        for (uint16_t field : {parsed.Format.Channels, parsed.Format.Bits})
        {
            byte(static_cast<uint8_t>(field));
            byte(static_cast<uint8_t>(field >> 8));
        }

        for (uint8_t sample : parsed.Samples)
        {
            byte(sample);
        }

        return std::format("{:016x}", hash);
    }

    /// <summary>The replacement for <paramref name="original"/> decoded from the pack's file, or null when it has none
    /// that is whole, of a sane size and as long as the original.</summary>
    std::shared_ptr<const MCHdAudio::Decoded> Decode(const MCWaveData& original, const std::string& key)
    {
        const auto path = Existing(MCHdArt::PackFolder() / "audio" / "samples" / (key + ".flac"));

        if (!path.has_value())
        {
            return nullptr;
        }

        const std::unique_ptr<MCFlac> flac = MCFlac::Open(*path);

        if (flac == nullptr || flac->Frames() * flac->Format().BlockAlign() > MaxSampleBytes ||
            !MCHdAudio::MatchesLength(*flac, original.Samples.size() / original.Format.BlockAlign(),
                                      original.Format.Rate))
        {
            return nullptr;
        }

        auto decoded = std::make_shared<MCHdAudio::Decoded>();
        decoded->Format = flac->Format();
        decoded->Pcm.resize(static_cast<size_t>(flac->Frames() * flac->Format().BlockAlign()));
        return flac->Read(decoded->Pcm) == decoded->Pcm.size() ? decoded : nullptr;
    }
}

std::optional<std::filesystem::path> MCHdAudio::Music(std::string_view original)
{
    if (!Active())
    {
        return std::nullopt;
    }

    const std::string_view file = original.substr(original.find_last_of("/\\") + 1);
    std::string name(file.substr(0, file.find_last_of('.')));
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    // Only the game's MUSICnn names are eligible; never interpret caller paths inside the pack.
    if (name.size() != MusicNameLength || !name.starts_with(MusicPrefix) ||
        !std::isdigit(static_cast<unsigned char>(name[5])) || !std::isdigit(static_cast<unsigned char>(name[6])))
    {
        return std::nullopt;
    }

    const std::filesystem::path root = MCHdArt::PackFolder() / "audio" / "music";

    if (GHdAudio == HdAudioOrchestral)
    {
        if (auto path = Existing(root / "orchestral" / (name + ".flac")))
        {
            return path;
        }
    }

    return Existing(root / "stereo" / (name + ".flac"));
}

std::optional<std::filesystem::path> MCHdAudio::RainLoop()
{
    return Current().Readable ? Existing(MCHdArt::PackFolder() / "audio" / "ambience" / "rain.flac") : std::nullopt;
}

std::string MCHdAudio::SampleKey(std::span<const uint8_t> wave)
{
    const auto parsed = MCAudio::ParseWave(wave);
    return parsed ? KeyOf(*parsed) : std::string();
}

std::shared_ptr<const MCHdAudio::Decoded> MCHdAudio::Sample(std::span<const uint8_t> wave)
{
    if (!Active())
    {
        return nullptr;
    }

    const auto parsed = MCAudio::ParseWave(wave);

    if (!parsed)
    {
        return nullptr;
    }

    State& state = Current();
    std::string key = KeyOf(*parsed);

    if (const auto found = state.Samples.find(key); found != state.Samples.end())
    {
        state.Order.splice(state.Order.begin(), state.Order, found->second.second);
        return found->second.first;
    }

    std::shared_ptr<const Decoded> decoded = Decode(*parsed, key);
    state.Bytes += decoded ? decoded->Pcm.size() : 0;
    state.Order.push_front(key);
    state.Samples.emplace(std::move(key), std::pair(decoded, state.Order.begin()));

    // The least recently used go once past the budget (the one just found stays).
    while (state.Bytes > SampleCacheBudget && state.Order.size() > 1)
    {
        const auto oldest = state.Samples.find(state.Order.back());
        state.Bytes -= oldest->second.first ? oldest->second.first->Pcm.size() : 0;
        state.Samples.erase(oldest);
        state.Order.pop_back();
    }

    return decoded;
}

bool MCHdAudio::MatchesLength(const MCFlac& flac, uint64_t sourceFrames, uint32_t sourceRate)
{
    if (sourceRate == 0)
    {
        return false;
    }

    const uint64_t wanted = (sourceFrames * flac.Format().Rate + sourceRate / 2) / sourceRate;
    return flac.Frames() == wanted;
}

bool MCHdAudio::PackReadable()
{
    return Current().Readable;
}

bool MCHdAudio::HasOrchestral()
{
    return Current().Orchestral;
}

void MCHdAudio::Forget()
{
    TheState().Version.reset();
}
