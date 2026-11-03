#include "stdafx.h"
#include "sound/MCHdAudio.h"
#include "lib/MCFitIniFile.h"
#include "platform/MCFlac.h"
#include "sprite/MCHdArt.h"
#include <condition_variable>
#include <unordered_set>

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

    /// <summary>A replacement remembered (null: the wave has none).</summary>
    struct MCRemembered
    {
        std::shared_ptr<const MCHdAudio::Decoded> Decoded;
        /// <summary>Its place in State::Order, or none while it is kept (MCHdAudio::Prepare's: beyond the budget).</summary>
        std::optional<std::list<uint64_t>::iterator> Place;
    };

    /// <summary>A wave handed to the decoding thread, and whether its replacement is kept beyond the budget.</summary>
    struct MCDecodeJob
    {
        std::vector<uint8_t> Wave;
        bool Keep = false;
    };

    /// <summary>
    /// What was found in the pack in use: whether it is one, the samples looked up or decoded ahead so far, and the
    /// thread that decodes them ahead.
    /// </summary>
    struct State
    {
        /// <summary>The pack version (MCHdArt::PackVersion) the rest was found for.</summary>
        std::optional<uint64_t> Version;
        /// <summary>Whether the pack folder has an audio.cfg of a format this code reads.</summary>
        bool Readable = false;
        /// <summary>Whether that pack has orchestral music.</summary>
        bool Orchestral = false;
        /// <summary>The pack version the waves last handed to MCHdAudio::Prepare were for.</summary>
        std::optional<uint64_t> PreparedVersion;
        /// <summary>Guards what follows, which the game's thread and the decoding thread share.</summary>
        std::mutex Lock;
        /// <summary>Told of every decode ended and job taken.</summary>
        std::condition_variable_any Changed;
        /// <summary>The replacements by the waves' keys, and the keys not kept, most recently used first.</summary>
        std::unordered_map<uint64_t, MCRemembered> Samples;
        std::list<uint64_t> Order;
        /// <summary>The bytes of the replacements in Order (the kept ones aren't counted against the budget).</summary>
        size_t Bytes = 0;
        /// <summary>The waves being decoded, by either thread: a play of one waits for it.</summary>
        std::unordered_set<uint64_t> Decoding;
        /// <summary>The waves waiting for the decoding thread, and the pack folder they are found in.</summary>
        std::deque<MCDecodeJob> Jobs;
        std::filesystem::path JobsFolder;
        /// <summary>Set while the decoding thread works on a job.</summary>
        bool Working = false;
        /// <summary>Counts the times what was found was forgotten: a decode begun before is dropped.</summary>
        uint64_t Generation = 0;
        /// <summary>The decoding thread, started with the first job; last, so it stops before the rest goes.</summary>
        std::jthread Decoder;
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
            {
                std::scoped_lock lock(state.Lock);
                state.Samples.clear();
                state.Order.clear();
                state.Bytes = 0;
                state.Decoding.clear();
                state.Jobs.clear();
                ++state.Generation;
            }

            state.Changed.notify_all();

            state.PreparedVersion.reset();
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

    /// <summary>The pack's weather sound <paramref name="name"/> (in audio/ambience), whatever GHdAudio is, or none.</summary>
    std::optional<std::filesystem::path> Ambience(std::string_view name)
    {
        return Current().Readable ? Existing(MCHdArt::PackFolder() / "audio" / "ambience" / name) : std::nullopt;
    }

    /// <summary>The canonical key of a wave (MCHdAudio::SampleId).</summary>
    uint64_t KeyOf(const MCWaveData& parsed)
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

        return hash;
    }

    /// <summary>The name a key gives a replacement in the pack: 16 hex digits.</summary>
    std::string KeyName(uint64_t key)
    {
        return std::format("{:016x}", key);
    }

    /// <summary>The replacement for <paramref name="original"/> decoded from the pack's file, or null when it has none
    /// that is whole, of a sane size and as long as the original.</summary>
    std::shared_ptr<const MCHdAudio::Decoded> Decode(const MCWaveData& original, uint64_t key,
                                                     const std::filesystem::path& folder)
    {
        const auto path = Existing(folder / "audio" / "samples" / (KeyName(key) + ".flac"));

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

namespace
{
    /// <summary>
    /// Remembers <paramref name="decoded"/> as <paramref name="key"/>'s replacement, kept beyond the budget or as the
    /// most recently used; the least recently used go once past the budget (the one just remembered stays). Call with
    /// the state's lock held.
    /// </summary>
    void Remember(State& state, uint64_t key, std::shared_ptr<const MCHdAudio::Decoded> decoded, bool keep)
    {
        MCRemembered remembered{.Decoded = std::move(decoded)};

        if (!keep)
        {
            state.Bytes += remembered.Decoded ? remembered.Decoded->Pcm.size() : 0;
            state.Order.push_front(key);
            remembered.Place = state.Order.begin();
        }

        state.Samples.insert_or_assign(key, std::move(remembered));

        while (state.Bytes > SampleCacheBudget && state.Order.size() > 1)
        {
            const auto oldest = state.Samples.find(state.Order.back());
            state.Bytes -= oldest->second.Decoded ? oldest->second.Decoded->Pcm.size() : 0;
            state.Samples.erase(oldest);
            state.Order.pop_back();
        }
    }

    /// <summary>Keeps a remembered replacement beyond the budget from now on. Call with the state's lock held.</summary>
    void Keep(State& state, MCRemembered& remembered)
    {
        if (remembered.Place.has_value())
        {
            state.Bytes -= remembered.Decoded ? remembered.Decoded->Pcm.size() : 0;
            state.Order.erase(*remembered.Place);
            remembered.Place.reset();
        }
    }

    /// <summary>
    /// The decoding thread: takes the jobs in turn, decodes each wave's replacement without the lock (unless it is
    /// known, or being decoded on the game's thread), and remembers it, unless what was found was forgotten meanwhile.
    /// </summary>
    void DecodeAhead(std::stop_token stop)
    {
        State& state = TheState();
        std::unique_lock lock(state.Lock);

        while (state.Changed.wait(lock, stop, [&state] { return !state.Jobs.empty(); }))
        {
            MCDecodeJob job = std::move(state.Jobs.front());
            state.Jobs.pop_front();
            const uint64_t generation = state.Generation;
            const std::filesystem::path folder = state.JobsFolder;
            state.Working = true;
            lock.unlock();
            const auto parsed = MCAudio::ParseWave(job.Wave);
            const std::optional<uint64_t> key = parsed ? std::optional(KeyOf(*parsed)) : std::nullopt;
            lock.lock();
            // The game's thread may be decoding it: wait for that, then it is known.
            state.Changed.wait(lock, [&] { return !key || !state.Decoding.contains(*key); });

            if (key.has_value() && state.Generation == generation)
            {
                if (const auto found = state.Samples.find(*key); found != state.Samples.end())
                {
                    if (job.Keep)
                    {
                        Keep(state, found->second);
                    }
                }
                else
                {
                    state.Decoding.insert(*key);
                    lock.unlock();
                    std::shared_ptr<const MCHdAudio::Decoded> decoded = Decode(*parsed, *key, folder);
                    lock.lock();

                    if (state.Generation == generation)
                    {
                        Remember(state, *key, std::move(decoded), job.Keep);
                        state.Decoding.erase(*key);
                    }
                }
            }

            state.Working = false;
            state.Changed.notify_all();
        }
    }

    /// <summary>
    /// Hands <paramref name="jobs"/> to the decoding thread (starting it the first time), to decode from the pack in
    /// use. Call with HD audio active.
    /// </summary>
    void Enqueue(State& state, std::vector<MCDecodeJob> jobs)
    {
        std::scoped_lock lock(state.Lock);
        state.JobsFolder = MCHdArt::PackFolder();

        for (MCDecodeJob& job : jobs)
        {
            state.Jobs.push_back(std::move(job));
        }

        if (!state.Decoder.joinable())
        {
            state.Decoder = std::jthread(DecodeAhead);
        }

        state.Changed.notify_all();
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
    return Ambience("rain.flac");
}

std::optional<std::filesystem::path> MCHdAudio::Thunder()
{
    return Ambience("thunder.flac");
}

std::optional<uint64_t> MCHdAudio::SampleId(std::span<const uint8_t> wave)
{
    const auto parsed = MCAudio::ParseWave(wave);
    return parsed ? std::optional(KeyOf(*parsed)) : std::nullopt;
}

std::string MCHdAudio::SampleKey(std::span<const uint8_t> wave)
{
    const std::optional<uint64_t> id = SampleId(wave);
    return id.has_value() ? KeyName(*id) : std::string();
}

std::shared_ptr<const MCHdAudio::Decoded> MCHdAudio::Sample(std::span<const uint8_t> wave, std::optional<uint64_t> id)
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
    const uint64_t key = id.has_value() ? *id : KeyOf(*parsed);
    std::unique_lock lock(state.Lock);
    // One the decoding thread is on is waited for (a decode at most); one still waiting for it is decoded here.
    state.Changed.wait(lock, [&] { return !state.Decoding.contains(key); });

    if (const auto found = state.Samples.find(key); found != state.Samples.end())
    {
        if (found->second.Place.has_value())
        {
            state.Order.splice(state.Order.begin(), state.Order, *found->second.Place);
        }

        return found->second.Decoded;
    }

    // Only this thread forgets what was found, so what is decoded here is remembered.
    state.Decoding.insert(key);
    lock.unlock();
    std::shared_ptr<const Decoded> decoded = Decode(*parsed, key, MCHdArt::PackFolder());
    lock.lock();
    Remember(state, key, decoded, false);
    state.Decoding.erase(key);
    state.Changed.notify_all();
    return decoded;
}

void MCHdAudio::Prepare(std::vector<std::vector<uint8_t>> waves)
{
    if (!Active())
    {
        return;
    }

    State& state = Current();
    state.PreparedVersion = state.Version;
    std::vector<MCDecodeJob> jobs;
    jobs.reserve(waves.size());

    for (std::vector<uint8_t>& wave : waves)
    {
        jobs.push_back({.Wave = std::move(wave), .Keep = true});
    }

    Enqueue(state, std::move(jobs));
}

bool MCHdAudio::Prepared()
{
    return !Active() || Current().PreparedVersion == Current().Version;
}

void MCHdAudio::Prefetch(std::span<const uint8_t> wave)
{
    if (!Active())
    {
        return;
    }

    std::vector<MCDecodeJob> jobs;
    jobs.push_back({.Wave = std::vector(wave.begin(), wave.end()), .Keep = false});
    Enqueue(Current(), std::move(jobs));
}

void MCHdAudio::FinishPreparing()
{
    State& state = TheState();
    std::unique_lock lock(state.Lock);
    state.Changed.wait(lock, [&state] { return state.Jobs.empty() && !state.Working; });
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
