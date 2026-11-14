#include "stdafx.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCLz.h"
#include "lib/MCWorkers.h"
#include "platform/MCFileSystem.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace
{
    /// <summary>The helpers beside the reading thread, at most.</summary>
    constexpr size_t MostHelpers = 3;
    /// <summary>
    /// The files a part of the reads takes, in each archive's order (each part opens its own handles on the archives it
    /// reads: a stream can't be read from two threads at once).
    /// </summary>
    constexpr size_t FilesAPart = 64;

    /// <summary>
    /// A file's key: its game path upper-cased, its slashes as spelled (the archives match names so: MCFastFile::Find).
    /// </summary>
    std::string KeyOf(std::string_view gamePath)
    {
        std::string key;
        key.reserve(gamePath.size());

        for (const char c : gamePath)
        {
            key.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }

        return key;
    }

    /// <summary>
    /// <paramref name="key"/> with either slash a backslash: the loose file every spelling of it names (MCFileSystem
    /// takes both), which a write to any of them changes.
    /// </summary>
    std::string LooseOf(std::string_view key)
    {
        std::string loose(key);
        std::ranges::replace(loose, '/', '\\');
        return loose;
    }

    /// <summary>
    /// What the game's thread and the reads share, under Lock. A key asked for is in Pending until its read ends, then
    /// in Held if it was kept; a key dropped while pending goes in Dropped, and its read keeps nothing.
    /// </summary>
    struct State
    {
        std::mutex Lock;
        /// <summary>Told as each part of the reads ends.</summary>
        std::condition_variable Read;
        std::unordered_set<std::string> Pending;
        std::unordered_set<std::string> Dropped;
        std::unordered_map<std::string, MCFilesAhead::MCAheadFile> Held;
        /// <summary>Whether a read ahead is pending or held (Drop and Take look first, without the lock).</summary>
        std::atomic<bool> Any = false;
        size_t Taken = 0;
        /// <summary>The noting thread (none: not noting), and the files noted, by key and in order.</summary>
        std::optional<std::thread::id> Noting;
        std::unordered_set<std::string> NotedKeys;
        std::vector<std::string> Noted;
        /// <summary>The reading thread; last, so it stops before the rest goes.</summary>
        std::jthread Reader;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>
    /// <paramref name="gamePath"/> looked for as MCFile::Open looks for a file to read, past the loose files: the bytes
    /// of the first archive's entry of that name (through <paramref name="handles"/>, a handle of this thread's own on
    /// each archive, opened as needed), not found, or none when MCFile::Open must look itself (a file put, loose or in
    /// memory; an entry larger than MostFileBytes, or than <paramref name="room"/> leaves).
    /// </summary>
    std::optional<MCFilesAhead::MCAheadFile> LookFor(const std::string& gamePath, MCFastFileSet& archives,
                                                     std::vector<std::optional<MCDiskFile>>& handles,
                                                     std::atomic<size_t>& room)
    {
        if (MCFileSystem::PutsImage(gamePath) ||
            MCDiskFile::Open(MCFileSystem::Resolve(gamePath), MCDiskFileMode::Read).has_value() ||
            MCFileSystem::FindImage(gamePath).has_value())
        {
            return std::nullopt;
        }

        const std::vector<std::unique_ptr<MCFastFile>>& files = archives.Files();
        handles.resize(files.size());

        for (size_t archive = 0; archive < files.size(); ++archive)
        {
            const MCFastFile& fastFile = *files[archive];
            const std::optional<int32_t> index = fastFile.Find(gamePath);

            if (!index.has_value())
            {
                continue;
            }

            const MCFileEntry& entry = fastFile.GetEntries()[static_cast<size_t>(*index)];
            const auto size = static_cast<size_t>(std::max(entry.RealSize, 0));
            size_t left = room.load();

            do
            {
                if (size > MCFilesAhead::MostFileBytes || size > left)
                {
                    return std::nullopt;
                }
            } while (!room.compare_exchange_weak(left, left - size));

            if (!handles[archive].has_value())
            {
                handles[archive] = fastFile.OpenAnother();
            }

            if (!handles[archive].has_value())
            {
                room += size;
                return std::nullopt;
            }

            // As MCFastFileSet::Read reads it: a packed entry that doesn't unpack fully keeps what did, the rest zero.
            std::vector<uint8_t> stored = fastFile.ReadStored(*index, *handles[archive]);
            MCFilesAhead::MCAheadFile found{.Found = true, .Bytes = std::vector<uint8_t>(size)};

            if (entry.Size == entry.RealSize)
            {
                std::ranges::copy(std::span(stored).first(std::min(stored.size(), size)), found.Bytes.begin());
            }
            else
            {
                LZDecomp(found.Bytes, stored);
            }

            return found;
        }

        return MCFilesAhead::MCAheadFile{};
    }

    /// <summary>
    /// <paramref name="gamePaths"/> in the order they lie in <paramref name="archives"/>: by the first archive holding
    /// each, then its place there, those none holds last; so that a part reads one archive's neighbouring entries in
    /// turn, as HD art's readers read theirs.
    /// </summary>
    void InArchiveOrder(std::vector<std::string>& gamePaths, const MCFastFileSet& archives)
    {
        struct Place
        {
            size_t Archive = 0;
            int32_t Offset = 0;
        };

        const std::vector<std::unique_ptr<MCFastFile>>& files = archives.Files();
        std::vector<std::pair<Place, std::string>> placed;
        placed.reserve(gamePaths.size());

        for (std::string& gamePath : gamePaths)
        {
            Place place{.Archive = files.size()};

            for (size_t archive = 0; archive < files.size(); ++archive)
            {
                if (const std::optional<int32_t> index = files[archive]->Find(gamePath); index.has_value())
                {
                    place = {archive, files[archive]->GetEntries()[static_cast<size_t>(*index)].Offset};
                    break;
                }
            }

            placed.emplace_back(place, std::move(gamePath));
        }

        std::ranges::stable_sort(
            placed, [](const auto& a, const auto& b)
            { return std::tie(a.first.Archive, a.first.Offset) < std::tie(b.first.Archive, b.first.Offset); });
        gamePaths.clear();

        for (auto& [place, gamePath] : placed)
        {
            gamePaths.push_back(std::move(gamePath));
        }
    }

    /// <summary>Reads part <paramref name="part"/> of <paramref name="gamePaths"/>, keeping each file not dropped meanwhile.</summary>
    void ReadPart(const std::vector<std::string>& gamePaths, size_t part, MCFastFileSet& archives,
                  std::atomic<size_t>& room, const std::stop_token& stop)
    {
        State& state = TheState();
        std::vector<std::optional<MCDiskFile>> handles;
        const size_t end = std::min(gamePaths.size(), (part + 1) * FilesAPart);

        for (size_t file = part * FilesAPart; file < end; ++file)
        {
            std::optional<MCFilesAhead::MCAheadFile> found;

            if (!stop.stop_requested())
            {
                try
                {
                    found = LookFor(gamePaths[file], archives, handles, room);
                }
                catch (const std::exception& error)
                {
                    SDL_Log("MCFilesAhead: reading %s ahead failed: %s (left to its open)", gamePaths[file].c_str(),
                            error.what());
                }
            }

            const std::string key = KeyOf(gamePaths[file]);
            std::scoped_lock lock(state.Lock);

            if (state.Pending.erase(key) != 0 && !state.Dropped.contains(key) && found.has_value())
            {
                state.Held.insert_or_assign(key, std::move(*found));
            }

            state.Read.notify_all();
        }
    }
}

void MCFilesAhead::StartNoting()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    state.Noting = std::this_thread::get_id();
}

void MCFilesAhead::StopNoting()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    state.Noting.reset();
}

void MCFilesAhead::Looked(std::string_view gamePath)
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);

    if (state.Noting == std::this_thread::get_id() && state.NotedKeys.insert(KeyOf(gamePath)).second)
    {
        state.Noted.emplace_back(gamePath);
    }
}

std::vector<std::string> MCFilesAhead::Noted()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    return state.Noted;
}

void MCFilesAhead::ReadAhead(std::vector<std::string> gamePaths, MCFastFileSet& archives)
{
    Forget();

    if (gamePaths.empty())
    {
        return;
    }

    State& state = TheState();

    {
        std::scoped_lock lock(state.Lock);

        for (const std::string& gamePath : gamePaths)
        {
            state.Pending.insert(KeyOf(gamePath));
        }

        state.Any = true;
    }

    state.Reader = std::jthread(
        [paths = std::move(gamePaths), &archives](const std::stop_token& stop) mutable
        {
            // Each file claims its room as it's read: past MostBytes in all, the rest are left to their opens.
            std::atomic<size_t> room = MostBytes;

            try
            {
                InArchiveOrder(paths, archives);
                MCWorkers helpers(MCWorkers::HelpersFor(MostHelpers));
                const size_t parts = (paths.size() + FilesAPart - 1) / FilesAPart;
                helpers.Run(parts, [&](size_t part) { ReadPart(paths, part, archives, room, stop); });
            }
            catch (const std::exception& error)
            {
                SDL_Log("MCFilesAhead: reading %zu files ahead failed: %s (left to their opens)", paths.size(),
                        error.what());
            }

            // Whatever a failed part left pending is its open's (this read ahead's are the only ones pending).
            State& state = TheState();
            std::scoped_lock lock(state.Lock);
            state.Pending.clear();
            state.Read.notify_all();
        });
}

std::optional<MCFilesAhead::MCAheadFile> MCFilesAhead::Take(std::string_view gamePath)
{
    State& state = TheState();

    if (!state.Any)
    {
        return std::nullopt;
    }

    const std::string key = KeyOf(gamePath);
    std::unique_lock lock(state.Lock);
    state.Read.wait(lock, [&] { return !state.Pending.contains(key); });
    const auto found = state.Held.find(key);

    if (found == state.Held.end())
    {
        return std::nullopt;
    }

    ++state.Taken;
    return found->second;
}

void MCFilesAhead::Drop(std::string_view gamePath)
{
    State& state = TheState();

    if (!state.Any)
    {
        return;
    }

    // Every spelling of the path, as a write to one is a write to all.
    const std::string loose = LooseOf(KeyOf(gamePath));
    std::scoped_lock lock(state.Lock);
    std::erase_if(state.Held, [&](const auto& held) { return LooseOf(held.first) == loose; });

    for (const std::string& pending : state.Pending)
    {
        if (LooseOf(pending) == loose)
        {
            state.Dropped.insert(pending);
        }
    }
}

void MCFilesAhead::Forget()
{
    State& state = TheState();

    if (state.Reader.joinable())
    {
        state.Reader.request_stop();
        state.Reader.join();
    }

    std::scoped_lock lock(state.Lock);
    state.Pending.clear();
    state.Dropped.clear();
    state.Held.clear();
    state.Any = false;
}

size_t MCFilesAhead::TakenCount()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    return state.Taken;
}

size_t MCFilesAhead::HeldCount()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    return state.Held.size();
}
