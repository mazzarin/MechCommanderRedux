#include "stdafx.h"
#include "sound/MCSoundReplacements.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFlac.h"
#include "platform/MCUtf8Path.h"

namespace
{
    /// <summary>How often the folder's files are looked at again while sounds play.</summary>
    constexpr auto ListingInterval = std::chrono::seconds(1);
    /// <summary>The most a replacement may decode to (a music track of 25 minutes at 48 kHz in stereo is 288 MB).</summary>
    constexpr uint64_t MostDecodedBytes = 512ull * 1024u * 1024u;
    /// <summary>The replacements' kinds, by extension (case ignored).</summary>
    constexpr std::string_view WaveExtension = ".wav";
    constexpr std::string_view FlacExtension = ".flac";
    /// <summary>A music track's name: MUSIC and two digits.</summary>
    constexpr std::string_view MusicPrefix = "MUSIC";
    constexpr size_t MusicNameLength = 7;
    /// <summary>The packets' files in the sound folders.</summary>
    constexpr std::string_view PacketExtension = ".pak";
    /// <summary>The most channels a replacement plays in (more are mixed to two), and the rates the mixer takes.</summary>
    constexpr int MostChannels = 2;
    constexpr int LowestRate = 100;
    constexpr int HighestRate = 200000;
    /// <summary>The bits a replacement is decoded to.</summary>
    constexpr uint16_t DecodedBits = 16;
    /// <summary>The other names a heard line gives for a recording kept in several packets, before "and n more".</summary>
    constexpr size_t MostOtherNames = 3;

    /// <summary>A file of the folder as it was seen: where, how long and when last written.</summary>
    struct MCFileSeen
    {
        std::filesystem::path Path;
        uintmax_t Size = 0;
        std::filesystem::file_time_type Written;

        bool operator==(const MCFileSeen&) const = default;
    };

    /// <summary>A replacement decoded from a file as it was seen (null: it couldn't be used).</summary>
    struct MCDecodedFile
    {
        MCFileSeen From;
        std::shared_ptr<const MCHdAudio::Decoded> Decoded;
    };

    struct State
    {
        /// <summary>Set while the folder is there: the sounds' lookups skip the lock without it.</summary>
        std::atomic<bool> Active = false;
        /// <summary>Guards the rest (the game's thread makes the sounds; the lock is for safety).</summary>
        std::mutex Lock;
        /// <summary>The folder, while it is there.</summary>
        std::optional<std::filesystem::path> Folder;
        /// <summary>When its files were last looked at.</summary>
        std::optional<std::chrono::steady_clock::time_point> Listed;
        /// <summary>Its replacements, by the name they replace (in capitals, without the extension).</summary>
        std::map<std::string, MCFileSeen> Files;
        /// <summary>Whether the sound packets' names have been read.</summary>
        bool NamesRead = false;
        /// <summary>Each recording's packets' names, by its MCHdAudio::SampleId (the same PCM in several packets).</summary>
        std::unordered_map<uint64_t, std::vector<std::string>> Names;
        /// <summary>Every packet's name, a sound or an empty slot.</summary>
        std::set<std::string> Known;
        /// <summary>The empty slots' names: nothing plays them, so nothing can be replaced there.</summary>
        std::set<std::string> Empty;
        /// <summary>The replacements decoded, by name.</summary>
        std::map<std::string, MCDecodedFile> Decoded;
        /// <summary>What was said about the files, once each.</summary>
        std::set<std::string> Noted;
        /// <summary>The list of the sounds heard.</summary>
        std::ofstream Heard;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    std::string Capitals(std::string_view text)
    {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return out;
    }

    bool IsMusicName(std::string_view name)
    {
        return name.size() == MusicNameLength && name.starts_with(MusicPrefix) &&
               std::isdigit(static_cast<unsigned char>(name[5])) && std::isdigit(static_cast<unsigned char>(name[6]));
    }

    /// <summary>The time of day, for the heard list.</summary>
    std::string Clock()
    {
        const auto now = std::chrono::system_clock::now();
#if defined(__APPLE__)
        // libc++ has no std::chrono::current_zone() (MCServices.cpp's LocalFileTime does the same).
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        localtime_r(&t, &local);
        return std::format("{:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);
#else
        return std::format("{:%H:%M:%S}",
                           std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(now)));
#endif
    }

    /// <summary>Adds a line to the heard list (nothing while it isn't open).</summary>
    void Hear(State& state, std::string_view text)
    {
        if (state.Heard.is_open())
        {
            state.Heard << Clock() << "  " << text << '\n';
            state.Heard.flush();
        }
    }

    /// <summary>Says <paramref name="text"/> in the heard list once for <paramref name="key"/>.</summary>
    void Note(State& state, const std::string& key, std::string_view text)
    {
        if (state.Noted.insert(key).second)
        {
            Hear(state, std::format("! {}", text));
            SDL_Log("Sound replacements: %.*s", static_cast<int>(text.size()), text.data());
        }
    }

    /// <summary>
    /// Reads every sound packet's MCHdAudio::SampleId, by the name the catalogue gives it, from the sound folders'
    /// .pak files (each read once, as the game finds them).
    /// </summary>
    void ReadNames(State& state)
    {
        state.NamesRead = true;
        std::set<std::string> read;

        for (const std::string& folder : {SoundPath, CDsoundPath})
        {
            for (const std::string& file : MCFileSystem::FindFiles(GamePath(folder, "*", PacketExtension)))
            {
                const std::string stem = Capitals(std::string_view(file).substr(0, file.find_last_of('.')));

                if (!read.insert(stem).second)
                {
                    continue;
                }

                MCPacketFile packets;

                if (packets.Open(GamePath(folder, stem, PacketExtension)) != 0)
                {
                    SDL_Log("Sound replacements: %s%s can't be opened; its sounds have no names", stem.c_str(),
                            PacketExtension.data());
                    continue;
                }

                std::vector<uint8_t> bytes;

                for (int32_t packet = 0; packet < packets.GetNumPackets(); packet++)
                {
                    const std::string name = MCSoundReplacements::PacketName(stem, packet);
                    state.Known.insert(name);

                    if (packets.SeekPacket(packet) != 0 || packets.GetPacketSize() <= 0)
                    {
                        state.Empty.insert(name);
                        continue;
                    }

                    bytes.resize(static_cast<size_t>(packets.GetPacketSize()));

                    if (packets.ReadPacket(packet, bytes) != static_cast<int32_t>(bytes.size()))
                    {
                        continue;
                    }

                    if (const std::optional<uint64_t> id = MCHdAudio::SampleId(bytes); id.has_value())
                    {
                        state.Names[*id].push_back(name);
                    }
                }
            }
        }
    }

    /// <summary>A WAV decoded by SDL (any PCM or float WAV it reads), as signed 16-bit PCM in one or two channels.</summary>
    std::expected<std::shared_ptr<MCHdAudio::Decoded>, std::string> DecodeWave(const std::filesystem::path& path)
    {
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 length = 0;

        if (!SDL_LoadWAV(MCFileSystem::ToUtf8(path).c_str(), &spec, &data, &length))
        {
            return std::unexpected(std::format("it isn't a WAV SDL reads ({})", SDL_GetError()));
        }

        const std::unique_ptr<Uint8, decltype(&SDL_free)> loaded(data, &SDL_free);

        if (spec.freq < LowestRate || spec.freq > HighestRate)
        {
            return std::unexpected(
                std::format("its rate, {} Hz, is outside {} to {}", spec.freq, LowestRate, HighestRate));
        }

        const SDL_AudioSpec wanted{SDL_AUDIO_S16LE, std::min(spec.channels, MostChannels), spec.freq};
        Uint8* converted = nullptr;
        int convertedLength = 0;

        if (!SDL_ConvertAudioSamples(&spec, data, static_cast<int>(length), &wanted, &converted, &convertedLength))
        {
            return std::unexpected(std::format("it can't be converted ({})", SDL_GetError()));
        }

        const std::unique_ptr<Uint8, decltype(&SDL_free)> pcm(converted, &SDL_free);

        if (static_cast<uint64_t>(convertedLength) > MostDecodedBytes)
        {
            return std::unexpected(
                std::format("it decodes to {} MB, more than {}", convertedLength >> 20, MostDecodedBytes >> 20));
        }

        auto decoded = std::make_shared<MCHdAudio::Decoded>();
        decoded->Format = {static_cast<uint32_t>(wanted.freq), static_cast<uint16_t>(wanted.channels), DecodedBits};
        decoded->Pcm.assign(converted, converted + convertedLength);
        return decoded;
    }

    /// <summary>A FLAC decoded by dr_flac (MCFlac) as signed 16-bit PCM.</summary>
    std::expected<std::shared_ptr<MCHdAudio::Decoded>, std::string> DecodeFlac(const std::filesystem::path& path)
    {
        const std::unique_ptr<MCFlac> flac = MCFlac::Open(path);

        if (flac == nullptr)
        {
            return std::unexpected(std::string("it isn't a FLAC the game reads (16 or 24 bits, one or two channels)"));
        }

        const uint64_t bytes = flac->Frames() * flac->Format().BlockAlign();

        if (bytes > MostDecodedBytes)
        {
            return std::unexpected(
                std::format("it decodes to {} MB, more than {}", bytes >> 20, MostDecodedBytes >> 20));
        }

        auto decoded = std::make_shared<MCHdAudio::Decoded>();
        decoded->Format = flac->Format();
        decoded->Pcm.resize(static_cast<size_t>(bytes));

        if (flac->Read(decoded->Pcm) != decoded->Pcm.size())
        {
            return std::unexpected(std::string("it is cut short or corrupt"));
        }

        return decoded;
    }

    /// <summary>
    /// The replacement <paramref name="name"/>'s file decoded, as it is now: decoded again once it has changed; null
    /// (said once a file) when it can't be used.
    /// </summary>
    std::shared_ptr<const MCHdAudio::Decoded> DecodedFor(State& state, const std::string& name, const MCFileSeen& seen)
    {
        if (const auto found = state.Decoded.find(name); found != state.Decoded.end() && found->second.From == seen)
        {
            return found->second.Decoded;
        }

        const std::string extension = Capitals(MCFileSystem::ToUtf8(seen.Path.extension()));
        auto decoded = extension == Capitals(FlacExtension) ? DecodeFlac(seen.Path) : DecodeWave(seen.Path);
        std::shared_ptr<const MCHdAudio::Decoded> result;

        if (!decoded.has_value())
        {
            const std::string file = MCFileSystem::ToUtf8(seen.Path.filename());
            Note(state, std::format("{} {}", file, seen.Written.time_since_epoch().count()),
                 std::format("{} can't be used: {}; the original plays", file, decoded.error()));
        }
        else if ((*decoded)->Pcm.empty())
        {
            const std::string file = MCFileSystem::ToUtf8(seen.Path.filename());
            Note(state, std::format("{} {}", file, seen.Written.time_since_epoch().count()),
                 std::format("{} can't be used: it has no sound in it; the original plays", file));
        }
        else
        {
            result = std::move(*decoded);
        }

        state.Decoded.insert_or_assign(name, MCDecodedFile{.From = seen, .Decoded = result});
        return result;
    }

    /// <summary>Stops replacing and listing: the folder is gone.</summary>
    void Close(State& state)
    {
        state.Active = false;
        state.Folder.reset();
        state.Listed.reset();
        state.Files.clear();
        state.Decoded.clear();
        state.Noted.clear();
        state.Heard.close();
    }

    /// <summary>Looks at the folder's files again, at most once a <see cref="ListingInterval"/>.</summary>
    void List(State& state)
    {
        const auto now = std::chrono::steady_clock::now();

        if (!state.Folder.has_value() || (state.Listed.has_value() && now - *state.Listed < ListingInterval))
        {
            return;
        }

        state.Listed = now;
        std::map<std::string, MCFileSeen> files;
        std::error_code error;

        for (auto at = std::filesystem::directory_iterator(*state.Folder, error);
             !error && at != std::filesystem::directory_iterator(); at.increment(error))
        {
            std::error_code kind;

            if (!at->is_regular_file(kind))
            {
                continue;
            }

            const std::filesystem::path& path = at->path();
            const std::string extension = Capitals(MCFileSystem::ToUtf8(path.extension()));
            const std::string file = MCFileSystem::ToUtf8(path.filename());

            if (extension != Capitals(WaveExtension) && extension != Capitals(FlacExtension))
            {
                continue;
            }

            const std::string name = Capitals(MCFileSystem::ToUtf8(path.stem()));

            if (state.Empty.contains(name))
            {
                Note(state, file, std::format("{} is for an empty slot, which nothing plays", file));
                continue;
            }

            if (!state.Known.contains(name) && !IsMusicName(name))
            {
                Note(state, file,
                     std::format("{}: no sound has this name (the sound catalogue gives each sound's)", file));
                continue;
            }

            MCFileSeen seen{.Path = path, .Size = at->file_size(kind), .Written = at->last_write_time(kind)};

            if (const auto [found, added] = files.try_emplace(name, seen); !added)
            {
                // Both a .wav and a .flac: the WAV plays.
                Note(state, name, std::format("{} has both a WAV and a FLAC: the WAV plays", name));

                if (extension == Capitals(WaveExtension))
                {
                    found->second = std::move(seen);
                }
            }
        }

        if (error)
        {
            SDL_Log("Sound replacements: the folder can't be listed (%s); nothing is replaced",
                    error.message().c_str());
            Close(state);
            return;
        }

        state.Files = std::move(files);
    }

    /// <summary>The names for a heard line: the first, then up to MostOtherNames others.</summary>
    std::string NamesShown(const std::vector<std::string>& names)
    {
        std::string out = names.front();

        if (names.size() > 1)
        {
            out += " (also";

            for (size_t i = 1; i < names.size() && i <= MostOtherNames; i++)
            {
                out += " " + names[i];
            }

            if (names.size() > MostOtherNames + 1)
            {
                out += std::format(" and {} more", names.size() - MostOtherNames - 1);
            }

            out += ")";
        }

        return out;
    }
}

void MCSoundReplacements::Look()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    const std::filesystem::path folder = MCFileSystem::UserRoot() / FolderName;
    std::error_code error;

    if (!std::filesystem::is_directory(folder, error))
    {
        Close(state);
        return;
    }

    if (state.Folder == folder)
    {
        return;
    }

    Close(state);
    state.Folder = folder;

    if (!state.NamesRead)
    {
        ReadNames(state);
    }

    state.Heard.open(folder / HeardFile, std::ios::trunc);

    if (!state.Heard.is_open())
    {
        SDL_Log("Sound replacements: %s can't be written; the sounds heard aren't listed", HeardFile.data());
    }

    state.Heard
        << "The sounds MechCommander Redux HD plays, newest last, by the names the sound catalogue gives them.\n"
        << "\"replaced by\" names the file that played instead; a line with ! says why a file isn't used.\n\n";
    state.Heard.flush();
    state.Active = true;
    List(state);
}

bool MCSoundReplacements::Active()
{
    return TheState().Active;
}

std::shared_ptr<const MCHdAudio::Decoded> MCSoundReplacements::Sample(std::span<const uint8_t> wave,
                                                                      std::optional<uint64_t> id)
{
    State& state = TheState();

    if (!state.Active)
    {
        return nullptr;
    }

    std::scoped_lock lock(state.Lock);
    List(state);

    if (!state.Folder.has_value())
    {
        return nullptr;
    }

    const std::optional<uint64_t> key = id.has_value() ? id : MCHdAudio::SampleId(wave);
    const auto named = key.has_value() ? state.Names.find(*key) : state.Names.end();

    if (named == state.Names.end())
    {
        Hear(state, std::format("a sound in no PAK (its HD audio key {})", MCHdAudio::SampleKey(wave)));
        return nullptr;
    }

    for (const std::string& name : named->second)
    {
        if (const auto file = state.Files.find(name); file != state.Files.end())
        {
            if (std::shared_ptr<const MCHdAudio::Decoded> decoded = DecodedFor(state, name, file->second))
            {
                Hear(state, std::format("{}  replaced by {}", NamesShown(named->second),
                                        MCFileSystem::ToUtf8(file->second.Path.filename())));
                return decoded;
            }
        }
    }

    Hear(state, NamesShown(named->second));
    return nullptr;
}

std::shared_ptr<const MCHdAudio::Decoded> MCSoundReplacements::Music(std::string_view original)
{
    State& state = TheState();

    if (!state.Active)
    {
        return nullptr;
    }

    const std::string_view file = original.substr(original.find_last_of("/\\") + 1);
    const std::string name = Capitals(file.substr(0, file.find_last_of('.')));

    if (!IsMusicName(name))
    {
        return nullptr;
    }

    std::scoped_lock lock(state.Lock);
    List(state);
    const auto found = state.Files.find(name);

    if (found != state.Files.end())
    {
        if (std::shared_ptr<const MCHdAudio::Decoded> decoded = DecodedFor(state, name, found->second))
        {
            Hear(state, std::format("{}  replaced by {}", name, MCFileSystem::ToUtf8(found->second.Path.filename())));
            return decoded;
        }
    }

    Hear(state, name);
    return nullptr;
}

std::string MCSoundReplacements::PacketName(std::string_view pak, int32_t packet)
{
    return std::format("{}-{:03}", Capitals(pak), packet);
}

void MCSoundReplacements::Forget()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    Close(state);
    state.NamesRead = false;
    state.Names.clear();
    state.Known.clear();
    state.Empty.clear();
}
