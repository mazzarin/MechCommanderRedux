#include "stdafx.h"
#include "update/MCContentStage.h"
#include "lib/MCFilePatch.h"
#include "platform/MCFileSystem.h"
#include "update/MCUpdateDisk.h"

// miniz is built without stdio or time (MCCore.vcxproj, src/mac/CMakeLists.txt); its header is read the same.
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "miniz.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
    using MCContentPlan::MCAction;
    using MCContentPlan::MCStep;

    /// <summary>Steps of the plan staging borrows for its run.</summary>
    using MCSteps = std::vector<std::reference_wrapper<const MCStep>>;

    constexpr double BytesPerGigabyte = 1e9;
    /// <summary>What a stop ends staging with, as a source's Cancel ends its fetch (MCSource::Cancel).</summary>
    constexpr std::string_view StoppedError = "stopped";
    /// <summary>
    /// The most patches fetched at once, each by a worker with a source of its own (MCSource::Another), and so the
    /// most downloaded ahead of their applying, in order: a request's first byte takes 110-310 ms, so a thousand
    /// patches fetched one by one would wait minutes for nothing.
    /// </summary>
    constexpr size_t PatchWorkers = 8;
    /// <summary>How often staging tells its progress while the workers fetch.</summary>
    constexpr std::chrono::milliseconds ProgressEvery{100};

    /// <summary>
    /// A sum of sizes that can't wrap: one past the most a count holds stays there (a list's sizes are signed, but a
    /// count read wrong mustn't pass the space check as a small one).
    /// </summary>
    class MCBytesNeeded
    {
    public:
        void Add(uint64_t bytes)
        {
            Overflowed = Overflowed || bytes > std::numeric_limits<uint64_t>::max() - Total;
            Total = Overflowed ? Total : Total + bytes;
        }

        uint64_t Total = 0;
        bool Overflowed = false;
    };

    /// <summary>miniz reads the zip through this.</summary>
    size_t ReadZip(void* opaque, mz_uint64 offset, void* buffer, size_t count)
    {
        auto& file = *static_cast<std::ifstream*>(opaque);
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
        return static_cast<size_t>(file.gcount());
    }

    /// <summary>
    /// Where an entry is unpacked to (a file made anew), its bytes and hash as they're written, the most it may hold (the
    /// list's size: more isn't the release's, and stops it), whether writing failed (the disk's fault, not the zip's),
    /// and whether it was stopped (asked before each piece).
    /// </summary>
    struct MCEntrySink
    {
        MCEntrySink(MCNewFile file, uint64_t most, const std::function<bool()>& goOn)
            : File(std::move(file)), Most(most), GoOn(goOn)
        {
        }

        MCNewFile File;
        MCSha256::MCHasher Hasher;
        uint64_t Written = 0;
        uint64_t Most = 0;
        const std::function<bool()>& GoOn;
        bool WriteFailed = false;
        bool TooLong = false;
        bool Stopped = false;
    };

    /// <summary>miniz writes an entry, in order, through this.</summary>
    size_t WriteEntry(void* opaque, mz_uint64, const void* buffer, size_t count)
    {
        auto& sink = *static_cast<MCEntrySink*>(opaque);

        if (!sink.GoOn())
        {
            sink.Stopped = true;
            return 0;
        }

        if (count > sink.Most - sink.Written)
        {
            sink.TooLong = true;
            return 0;
        }

        const std::span<const char> bytes(static_cast<const char*>(buffer), count);

        if (!sink.File.WriteAt(sink.Written, bytes))
        {
            sink.WriteFailed = true;
            return 0;
        }

        sink.Hasher.Add(bytes);
        sink.Written += bytes.size();
        return count;
    }

    /// <summary>
    /// A patch fetched once by its hosted name, and the steps it's applied to: it's named by the old file's hash and the
    /// new one's, so identical files changed alike share one.
    /// </summary>
    struct MCPatchJob
    {
        MCContentList::MCPatch Patch;
        MCSteps Steps;
    };

    class MCStager
    {
    public:
        MCStager(MCUpdateDisk& disk, MCContentStage::MCSource& source, std::filesystem::path game,
                 const MCContentPlan::MCPlan& plan, const MCContentList::MCList& list,
                 const MCUpdateWork::MCProgressCallback& progress)
            : _Disk(disk)
            , _Source(source)
            , _Game(std::move(game))
            , _NoLinks(disk, _Game)
            , _Plan(plan)
            , _List(list)
            , _Progress(progress)
            , _Expected(plan.DownloadBytes)
        {
        }

        /// <summary>When staging stopped for want of space: how many more bytes the disk must have free.</summary>
        std::optional<uint64_t> BytesShort() const { return _Short; }

        std::expected<void, std::string> Run()
        {
            // Staging over a ready update would change files its journal moves: that update swaps in first. One that
            // can't be looked for is never cleared away.
            if (const auto waiting = MCContentSwap::Waiting(_Disk, _Game); !waiting || *waiting)
            {
                return std::unexpected(waiting
                                           ? std::string("an update is already staged; it goes in at the next start")
                                           : waiting.error());
            }

            if (auto cleared = ClearUnplanned(); !cleared)
            {
                return cleared;
            }

            // What an earlier attempt left counts as in hand from the first report, while it's checked.
            _InHand = InHandBySize();
            _Checking = _InHand > 0;

            // What an earlier attempt staged as the list has it stays; the rest is still to do.
            std::vector<MCPatchJob> patches;
            MCSteps whole;
            std::map<std::string_view, uint64_t> patchesStaged;

            for (const MCStep& step : _Plan.Steps)
            {
                if (step.Action != MCAction::Patch && step.Action != MCAction::Fetch)
                {
                    continue;
                }

                const auto staged = FoundStaged(step);

                if (!staged)
                {
                    return std::unexpected(staged.error());
                }

                if (*staged)
                {
                    if (step.Action == MCAction::Patch)
                    {
                        patchesStaged.emplace(step.Patch->Name, step.Patch->Size);
                    }
                }
                else if (step.Action == MCAction::Fetch)
                {
                    whole.push_back(std::cref(step));
                }
                else if (const auto job = std::ranges::find(patches, step.Patch->Name,
                                                            [](const MCPatchJob& job) { return job.Patch.Name; });
                         job != patches.end())
                {
                    job->Steps.push_back(std::cref(step));
                }
                else
                {
                    patches.push_back({.Patch = *step.Patch, .Steps = {std::cref(step)}});
                }
            }

            // A patch whose every file an earlier attempt staged counts as in.
            for (const auto& [name, size] : patchesStaged)
            {
                if (std::ranges::none_of(patches, [&](const MCPatchJob& job) { return job.Patch.Name == name; }))
                {
                    Received(size);
                }
            }

            if (Stopped())
            {
                return Stop();
            }

            const std::map<std::string_view, uint64_t> zipsPlanned = ZipsHolding(whole);

            if (auto space = CheckSpace(patches, whole); !space)
            {
                return space;
            }

            if (auto patched = PatchAll(patches, whole); !patched)
            {
                return patched;
            }

            // A patch that fell back on a zip not otherwise fetched needs room for it too.
            if (!std::ranges::includes(zipsPlanned, ZipsHolding(whole)))
            {
                if (auto space = CheckSpace({}, whole); !space)
                {
                    return space;
                }
            }

            for (const MCContentList::MCZip& zip : _List.Zips)
            {
                MCSteps entries;
                std::ranges::copy_if(whole, std::back_inserter(entries),
                                     [&](const MCStep& step) { return step.File->Zip == zip.Name; });

                if (entries.empty())
                {
                    // Every file it holds staged by an earlier attempt: it counts as in.
                    if (std::ranges::find(_Plan.Zips, zip.Name, &MCContentList::MCZip::Name) != _Plan.Zips.end())
                    {
                        Received(zip.Size);
                    }

                    continue;
                }

                // A zip the plan didn't count (a patch fell back on it) adds to what's downloaded.
                if (std::ranges::find(_Plan.Zips, zip.Name, &MCContentList::MCZip::Name) == _Plan.Zips.end())
                {
                    _Expected += zip.Size;
                }

                if (const auto unpacked = FetchAndUnpack(zip, entries); !unpacked)
                {
                    return Stopped() ? Stop() : unpacked;
                }
            }

            return Flush();
        }

    private:
        std::filesystem::path At(std::string_view relative) const { return _Game / MCFileSystem::FromUtf8(relative); }

        std::filesystem::path StagedPath(const MCStep& step) const
        {
            return At(MCContentSwap::StagedFolder) / MCFileSystem::FromUtf8(step.OnDisk);
        }

        std::filesystem::path DownloadPath(std::string_view name) const
        {
            return At(MCContentStage::DownloadFolder) / MCFileSystem::FromUtf8(name);
        }

        /// <summary>
        /// Whether the step's file is staged as the list has it, found from an earlier attempt. A file there that isn't
        /// (cut short by a crash, or changed since) is deleted, so it takes no room the space check counts again; a
        /// folder is left as it is, for the unpacking to fail on.
        /// </summary>
        /// <returns>Whether it's staged, or why it can't be told or cleared (stopped).</returns>
        std::expected<bool, std::string> FoundStaged(const MCStep& step)
        {
            const std::filesystem::path path = StagedPath(step);

            if (auto checked = _NoLinks.Check(path); !checked)
            {
                return std::unexpected(checked.error());
            }

            const MCUpdateDisk::MCStat stat = _Disk.Stat(path);

            if (!stat.Error.empty())
            {
                return std::unexpected(stat.Error);
            }

            if (!stat.Exists || stat.Folder)
            {
                return false;
            }

            if (stat.Size == step.File->Size)
            {
                const auto hash = MCSha256::OfFile(path, _GoOn);

                if (Stopped())
                {
                    return Stop();
                }

                if (hash && *hash == step.File->Sha256)
                {
                    return true;
                }
            }

            if (!_Disk.Remove(path))
            {
                return std::unexpected(
                    std::format("{}, staged by an earlier attempt and not the release's, can't be deleted",
                                MCFileSystem::ToUtf8(path)));
            }

            SDL_Log("Updates: %s, staged by an earlier attempt, isn't the release's: it's staged again",
                    step.Path.c_str());
            return false;
        }

        /// <summary>Whether the step's file is staged as the list has it (just patched).</summary>
        bool IsStaged(const MCStep& step)
        {
            const MCUpdateDisk::MCStat stat = _Disk.Stat(StagedPath(step));

            if (!stat.Exists || stat.Folder || stat.Size != step.File->Size)
            {
                return false;
            }

            const auto hash = MCSha256::OfFile(StagedPath(step), _GoOn);
            return hash && *hash == step.File->Sha256;
        }

        /// <summary>
        /// Deletes what's in staged/ and download/ that this plan doesn't name: an update dropped midway (a release
        /// replaced since, HD's download stopped) leaves nothing behind, however big. Logged with what it freed.
        /// </summary>
        std::expected<void, std::string> ClearUnplanned()
        {
            std::set<std::string, std::less<>> staged;
            std::set<std::string, std::less<>> downloads;

            for (const MCStep& step : _Plan.Steps)
            {
                if (step.Action != MCAction::Fetch && step.Action != MCAction::Patch)
                {
                    continue;
                }

                staged.insert(MCContentList::FoldedPath(step.OnDisk));
                downloads.insert(MCContentList::FoldedPath(step.File->Zip));

                if (step.Patch)
                {
                    downloads.insert(MCContentList::FoldedPath(step.Patch->Name));
                }
            }

            uint64_t freed = 0;
            size_t files = 0;

            for (const auto& [folder, named] : {std::pair{At(MCContentSwap::StagedFolder), &staged},
                                                std::pair{At(MCContentStage::DownloadFolder), &downloads}})
            {
                const auto found = _Disk.Files(folder);

                if (!found)
                {
                    return std::unexpected(found.error());
                }

                for (const std::string& relative : *found)
                {
                    if (named->contains(MCContentList::FoldedPath(relative)))
                    {
                        continue;
                    }

                    const std::filesystem::path path = folder / MCFileSystem::FromUtf8(relative);
                    const uint64_t size = _Disk.Stat(path).Size;

                    if (!_Disk.Remove(path))
                    {
                        return std::unexpected(
                            std::format("{}, left by an earlier update, can't be deleted", MCFileSystem::ToUtf8(path)));
                    }

                    freed += size;
                    ++files;
                }
            }

            if (files != 0)
            {
                SDL_Log("Updates: %zu files an earlier update left in %s cleared, %llu bytes freed", files,
                        MCContentSwap::UpdateFolder.data(), static_cast<unsigned long long>(freed));
            }

            return {};
        }

        /// <summary>The zips holding <paramref name="steps"/>' files: each one's size, by its name.</summary>
        std::map<std::string_view, uint64_t> ZipsHolding(const MCSteps& steps) const
        {
            std::map<std::string_view, uint64_t> zips;

            for (const MCContentList::MCZip& zip : _List.Zips)
            {
                if (std::ranges::any_of(steps, [&](const MCStep& step) { return step.File->Zip == zip.Name; }))
                {
                    zips.emplace(zip.Name, zip.Size);
                }
            }

            return zips;
        }

        /// <summary>Whether the step's file is in staged/ at its size (not yet checked).</summary>
        bool StagedBySize(const MCStep& step)
        {
            const MCUpdateDisk::MCStat stat = _Disk.Stat(StagedPath(step));
            return stat.Exists && !stat.Folder && stat.Size == step.File->Size;
        }

        /// <summary>
        /// The bytes an earlier attempt left, by their sizes alone (they're checked next): each zip and patch whose
        /// files are all staged at their sizes, and what's downloaded of the rest. What's downloaded counts as in from
        /// here (<see cref="_Prior"/>); the staged files count once they're checked.
        /// </summary>
        uint64_t InHandBySize()
        {
            // Each hosted file the plan fetches: its size, and whether every file it brings is staged.
            std::map<std::string_view, std::pair<uint64_t, bool>> hosted;

            for (const MCContentList::MCZip& zip : _Plan.Zips)
            {
                hosted.try_emplace(zip.Name, zip.Size, true);
            }

            for (const MCStep& step : _Plan.Steps)
            {
                const bool fetched = step.Action == MCAction::Fetch;

                if (!fetched && step.Action != MCAction::Patch)
                {
                    continue;
                }

                const std::string_view name = fetched ? std::string_view(step.File->Zip) : step.Patch->Name;
                const uint64_t size = fetched ? 0 : step.Patch->Size;
                bool& staged = hosted.try_emplace(name, size, true).first->second.second;
                staged = staged && StagedBySize(step);
            }

            uint64_t inHand = 0;

            for (const auto& [name, file] : hosted)
            {
                const auto& [size, staged] = file;

                if (staged)
                {
                    inHand += size;
                    continue;
                }

                const uint64_t downloaded = Downloaded(name, size);
                _Prior.emplace(name, downloaded);
                _Received += downloaded;
                inHand += downloaded;
            }

            return inHand;
        }

        /// <summary>The bytes of the hosted file <paramref name="name"/> already in download/ (a cut-off download's).</summary>
        uint64_t Downloaded(std::string_view name, uint64_t size)
        {
            const MCUpdateDisk::MCStat stat = _Disk.Stat(DownloadPath(name));
            return stat.Exists && !stat.Folder && stat.Size <= size ? stat.Size : 0;
        }

        /// <summary>
        /// Refuses to go on without room for what's still to stage (<paramref name="patches"/>' and <paramref
        /// name="whole"/>'s files), the biggest patches still to fetch (as many as are fetched ahead) and the biggest
        /// zip; what's already staged or downloaded takes its room already. When the free space can't be read, it's
        /// logged and staging goes on.
        /// </summary>
        std::expected<void, std::string> CheckSpace(const std::vector<MCPatchJob>& patches, const MCSteps& whole)
        {
            MCBytesNeeded needed;
            uint64_t biggestZip = 0;
            std::vector<uint64_t> patchesLeft;

            for (const MCPatchJob& job : patches)
            {
                for (const MCStep& step : job.Steps)
                {
                    needed.Add(step.File->Size);
                }

                patchesLeft.push_back(job.Patch.Size - Downloaded(job.Patch.Name, job.Patch.Size));
            }

            const size_t ahead = std::min(PatchWorkers, patchesLeft.size());
            std::ranges::partial_sort(patchesLeft, patchesLeft.begin() + static_cast<std::ptrdiff_t>(ahead),
                                      std::greater<>());

            for (size_t i = 0; i < ahead; ++i)
            {
                needed.Add(patchesLeft[i]);
            }

            for (const MCStep& step : whole)
            {
                needed.Add(step.File->Size);
            }

            for (const auto& [name, size] : ZipsHolding(whole))
            {
                biggestZip = std::max(biggestZip, size - Downloaded(name, size));
            }

            needed.Add(biggestZip);

            if (needed.Overflowed)
            {
                return std::unexpected("the update's sizes add up to more than any disk holds");
            }

            const auto free = _Disk.FreeBytes(_Game);

            if (!free)
            {
                SDL_Log("Updates: staging goes on unchecked: %s", free.error().c_str());
                return {};
            }

            if (*free < needed.Total)
            {
                _Short = needed.Total - *free;
                return std::unexpected(
                    std::format("the update needs {:.1f} GB free on the game's disk; it has {:.1f} GB",
                                static_cast<double>(needed.Total) / BytesPerGigabyte,
                                static_cast<double>(*free) / BytesPerGigabyte));
            }

            return {};
        }

        /// <summary>
        /// Downloads a hosted file (resuming) from <paramref name="source"/>, and checks it's the list's; a stop ends it
        /// at once. <paramref name="received"/> counts the bytes (Received on staging's own thread, Counted on a
        /// worker's).
        /// </summary>
        std::expected<std::filesystem::path, std::string> FetchChecked(MCContentStage::MCSource& source,
                                                                       std::string_view name, uint64_t size,
                                                                       const MCSha256::MCDigest& sha256,
                                                                       const std::function<bool(uint64_t)>& received)
        {
            const std::filesystem::path path = DownloadPath(name);

            if (auto checked = _NoLinks.Check(path); !checked)
            {
                return std::unexpected(checked.error());
            }

            const auto fetched = MCContentStage::FetchChecked(_Disk, source, name, path, size, sha256, received);

            if (fetched)
            {
                return path;
            }

            switch (fetched.error().Kind)
            {
                case MCContentStage::MCFetchError::MCKind::Stopped:
                    return std::unexpected(std::string(StoppedError));
                case MCContentStage::MCFetchError::MCKind::NotAsNamed:
                    return std::unexpected(std::format("{} isn't as the list has it", name));
                case MCContentStage::MCFetchError::MCKind::Download:
                    break;
            }

            return std::unexpected(std::format("{} didn't download: {}", name, fetched.error().Reason));
        }

        /// <summary>
        /// Stages <paramref name="patches"/>' files, each patch fetched once and applied in order to each of its files;
        /// a file whose patch doesn't come or doesn't apply joins <paramref name="whole"/>. With a source that makes
        /// others, the patches are fetched ahead by workers, <see cref="PatchWorkers"/> at once; otherwise one by one.
        /// </summary>
        /// <returns>Nothing, or stopped.</returns>
        std::expected<void, std::string> PatchAll(const std::vector<MCPatchJob>& patches, MCSteps& whole)
        {
            std::vector<std::shared_ptr<MCContentStage::MCSource>> sources;

            while (patches.size() > 1 && sources.size() < std::min(PatchWorkers, patches.size()))
            {
                std::unique_ptr<MCContentStage::MCSource> another = _Source.Another();

                if (another == nullptr)
                {
                    break;
                }

                sources.push_back(std::move(another));
            }

            if (sources.empty())
            {
                for (const MCPatchJob& job : patches)
                {
                    const auto fetched = FetchPatch(_Source, job, CountFor(job.Patch.Name, false));

                    if (auto patched = Patched(job, fetched, whole); !patched)
                    {
                        return patched;
                    }
                }

                return {};
            }

            return PatchAhead(patches, sources, whole);
        }

        /// <summary>The patches fetched ahead, shared by the workers and staging's thread, which applies them.</summary>
        struct MCAhead
        {
            std::mutex Lock;
            std::condition_variable Changed;
            /// <summary>The next patch to fetch, and the patches applied so far (a worker fetches no further ahead).</summary>
            size_t Next = 0;
            size_t Applied = 0;
            bool Ending = false;
            std::vector<std::optional<std::expected<std::filesystem::path, std::string>>> Fetched;
        };

        /// <summary>The workers fetching ahead, ended and their sources let go of whichever way staging goes on.</summary>
        class MCWorkers
        {
        public:
            MCWorkers(MCStager& stager, MCAhead& ahead) : _Stager(stager), _Ahead(ahead) {}
            MCWorkers(const MCWorkers&) = delete;
            MCWorkers& operator=(const MCWorkers&) = delete;

            ~MCWorkers()
            {
                {
                    const std::scoped_lock lock(_Ahead.Lock);
                    _Ahead.Ending = true;
                }

                _Ahead.Changed.notify_all();
                Threads.clear();
                const std::scoped_lock lock(_Stager._WorkersLock);
                _Stager._Workers.clear();
            }

            std::vector<std::jthread> Threads;

        private:
            MCStager& _Stager;
            MCAhead& _Ahead;
        };

        std::expected<void, std::string> PatchAhead(const std::vector<MCPatchJob>& patches,
                                                    std::vector<std::shared_ptr<MCContentStage::MCSource>>& sources,
                                                    MCSteps& whole)
        {
            MCAhead ahead;
            ahead.Fetched.resize(patches.size());
            MCWorkers workers(*this, ahead);

            {
                // A stop cancels them, so a fetch that's stalled ends at once.
                const std::scoped_lock lock(_WorkersLock);

                for (const std::shared_ptr<MCContentStage::MCSource>& source : sources)
                {
                    _Workers.push_back(source);
                }
            }

            if (_Stopped)
            {
                return Stop();
            }

            for (const std::shared_ptr<MCContentStage::MCSource>& source : sources)
            {
                workers.Threads.emplace_back([this, &ahead, &patches, worker = source]
                                             { FetchAhead(*worker, ahead, patches); });
            }

            for (size_t index = 0; index < patches.size(); ++index)
            {
                std::expected<std::filesystem::path, std::string> fetched;

                {
                    std::unique_lock lock(ahead.Lock);

                    // The progress is told, and asked whether to go on, from this thread alone.
                    while (
                        !ahead.Changed.wait_for(lock, ProgressEvery, [&] { return ahead.Fetched[index].has_value(); }))
                    {
                        lock.unlock();

                        if (!Received(0))
                        {
                            return Stop();
                        }

                        lock.lock();
                    }

                    fetched = std::move(*ahead.Fetched[index]);
                }

                if (!Received(0))
                {
                    return Stop();
                }

                auto patched = Patched(patches[index], fetched, whole);

                {
                    const std::scoped_lock lock(ahead.Lock);
                    ahead.Applied = index + 1;
                }

                ahead.Changed.notify_all();

                if (!patched)
                {
                    return patched;
                }
            }

            return {};
        }

        /// <summary>A worker: fetches the next patch, no further ahead of those applied than there are workers.</summary>
        void FetchAhead(MCContentStage::MCSource& source, MCAhead& ahead, const std::vector<MCPatchJob>& patches)
        {
            for (;;)
            {
                size_t index = 0;

                {
                    std::unique_lock lock(ahead.Lock);
                    ahead.Changed.wait(lock,
                                       [&]
                                       {
                                           return ahead.Ending || ahead.Next >= patches.size() ||
                                                  ahead.Next < ahead.Applied + PatchWorkers;
                                       });

                    if (ahead.Ending || ahead.Next >= patches.size() || _Stopped)
                    {
                        return;
                    }

                    index = ahead.Next++;
                }

                auto fetched = FetchPatch(source, patches[index], CountFor(patches[index].Patch.Name, true));

                {
                    const std::scoped_lock lock(ahead.Lock);
                    ahead.Fetched[index] = std::move(fetched);
                }

                ahead.Changed.notify_all();
            }
        }

        /// <summary>
        /// Applies a patch fetched to each of its files; a file whose patch didn't come, or doesn't apply, is left to
        /// fetch whole (<paramref name="whole"/>). The patch goes once its last file is done; stopped, it stays for
        /// the next attempt.
        /// </summary>
        /// <returns>Nothing, or stopped: a stop isn't a patch that failed, so nothing more is fetched or applied.</returns>
        std::expected<void, std::string> Patched(const MCPatchJob& job,
                                                 const std::expected<std::filesystem::path, std::string>& fetched,
                                                 MCSteps& whole)
        {
            for (const MCStep& step : job.Steps)
            {
                std::expected<void, std::string> patched = std::unexpected(fetched.error_or(std::string()));

                if (fetched)
                {
                    patched = Apply(step, *fetched);
                }

                if (patched)
                {
                    continue;
                }

                if (Stopped())
                {
                    return Stop();
                }

                SDL_Log("Updates: %s is fetched whole: %s", step.Path.c_str(), patched.error().c_str());
                whole.push_back(std::cref(step));
            }

            if (fetched && !_Disk.Remove(*fetched))
            {
                SDL_Log("Updates: %s, applied, can't be deleted; it goes with download/",
                        MCFileSystem::ToUtf8(*fetched).c_str());
            }

            return {};
        }

        /// <summary>Downloads a patch into download/, checked.</summary>
        std::expected<std::filesystem::path, std::string> FetchPatch(MCContentStage::MCSource& source,
                                                                     const MCPatchJob& job,
                                                                     const std::function<bool(uint64_t)>& received)
        {
            return FetchChecked(source, job.Patch.Name, job.Patch.Size, job.Patch.Sha256, received);
        }

        /// <summary>Writes the patched file to staged/; fails (and leaves nothing) when it can't.</summary>
        std::expected<void, std::string> Apply(const MCStep& step, const std::filesystem::path& patch)
        {
            const std::filesystem::path staged = StagedPath(step);

            if (auto checked = _NoLinks.Check(staged); !checked)
            {
                return checked;
            }

            if (!_Disk.MakeFolders(staged.parent_path()))
            {
                return std::unexpected(std::format("{} can't be made", MCFileSystem::ToUtf8(staged.parent_path())));
            }

            if (Stopped())
            {
                return Stop();
            }

            const auto applied = MCFilePatch::Apply(At(step.OnDisk), patch, staged, _GoOn, step.File->Size);

            if (Stopped())
            {
                return Stop();
            }

            if (!applied)
            {
                return std::unexpected(applied.error());
            }

            if (!IsStaged(step))
            {
                if (Stopped())
                {
                    return Stop();
                }

                _Disk.Remove(staged);
                return std::unexpected("the patched file isn't the release's");
            }

            return {};
        }

        /// <summary>
        /// Downloads a zip and unpacks the entries wanted from it into staged/, each checked. The zip goes once done, or
        /// when it's at fault; one whose entry couldn't be written (a full disk) stays for the next attempt.
        /// </summary>
        std::expected<void, std::string> FetchAndUnpack(const MCContentList::MCZip& zip, const MCSteps& entries)
        {
            const auto path = FetchChecked(_Source, zip.Name, zip.Size, zip.Sha256, CountFor(zip.Name, false));

            if (!path)
            {
                return std::unexpected(path.error());
            }

            if (Stopped())
            {
                return Stop();
            }

            std::ifstream file(*path, std::ios::binary);
            mz_zip_archive archive{};
            archive.m_pRead = ReadZip;
            archive.m_pIO_opaque = &file;

            if (!file || !mz_zip_reader_init(&archive, zip.Size, 0))
            {
                file.close();
                _Disk.Remove(*path);
                return std::unexpected(std::format("{} can't be opened as a zip", zip.Name));
            }

            std::string error;
            bool keepZip = false;

            for (const MCStep& step : entries)
            {
                // Stopped, the zip stays for the next attempt.
                if (Stopped())
                {
                    error = StoppedError;
                    keepZip = true;
                    break;
                }

                const int index =
                    mz_zip_reader_locate_file(&archive, step.File->Path.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
                const std::filesystem::path staged = StagedPath(step);

                if (index < 0)
                {
                    error = std::format("{} isn't in {}", step.File->Path, zip.Name);
                    break;
                }

                if (auto checked = _NoLinks.Check(staged); !checked)
                {
                    error = checked.error();
                    keepZip = true;
                    break;
                }

                if (!_Disk.MakeFolders(staged.parent_path()))
                {
                    error = std::format("{} can't be made", MCFileSystem::ToUtf8(staged.parent_path()));
                    keepZip = true;
                    break;
                }

                // Made anew: never through a link, nor over what an earlier attempt left.
                auto made = MCNewFile::Create(staged);

                if (!made)
                {
                    error = made.error();
                    keepZip = true;
                    break;
                }

                MCEntrySink sink(std::move(*made), step.File->Size, _GoOn);
                const bool unpacked =
                    mz_zip_reader_extract_to_callback(&archive, static_cast<mz_uint>(index), WriteEntry, &sink, 0);
                const bool closed = sink.File.Close();

                if (sink.Stopped)
                {
                    _Disk.Remove(staged);
                    error = StoppedError;
                    keepZip = true;
                    break;
                }

                if (!closed || sink.WriteFailed)
                {
                    _Disk.Remove(staged);
                    error = std::format("{} can't be written", MCFileSystem::ToUtf8(staged));
                    keepZip = true;
                    break;
                }

                if (!unpacked || sink.TooLong || sink.Written != step.File->Size ||
                    sink.Hasher.Finish() != step.File->Sha256)
                {
                    _Disk.Remove(staged);
                    error = std::format("{} from {} isn't the release's", step.File->Path, zip.Name);
                    break;
                }
            }

            mz_zip_reader_end(&archive);
            file.close();

            if (!keepZip)
            {
                _Disk.Remove(*path);
            }

            if (!error.empty())
            {
                return std::unexpected(error);
            }

            return {};
        }

        /// <summary>
        /// Flushes every staged file and the folders holding them (each a full flush to the disk, asked before whether
        /// to stop); the downloads go. Stopped, staging isn't done, so the update is never armed.
        /// </summary>
        std::expected<void, std::string> Flush()
        {
            std::set<std::filesystem::path> folders;
            const std::filesystem::path update = At(MCContentSwap::UpdateFolder);

            for (const MCStep& step : _Plan.Steps)
            {
                if (step.Action != MCAction::Fetch && step.Action != MCAction::Patch)
                {
                    continue;
                }

                const std::filesystem::path staged = StagedPath(step);

                if (Stopped())
                {
                    return Stop();
                }

                if (!_Disk.FlushFile(staged))
                {
                    return std::unexpected(std::format("{} can't be flushed", MCFileSystem::ToUtf8(staged)));
                }

                for (std::filesystem::path folder = staged.parent_path();
                     folder != update && folder.has_relative_path(); folder = folder.parent_path())
                {
                    folders.insert(folder);
                }
            }

            folders.insert(update);

            for (const std::filesystem::path& folder : folders)
            {
                if (Stopped())
                {
                    return Stop();
                }

                if (!_Disk.FlushFolder(folder))
                {
                    return std::unexpected(std::format("{} can't be flushed", MCFileSystem::ToUtf8(folder)));
                }
            }

            if (Stopped())
            {
                return Stop();
            }

            _Disk.Remove(At(MCContentStage::DownloadFolder));
            return {};
        }

        /// <summary>
        /// On staging's own thread: counts bytes in, tells the progress, and asks whether to go on. While what an
        /// earlier attempt left is checked, it's told as in hand already.
        /// </summary>
        bool Received(uint64_t count)
        {
            _Received += count;
            const bool checking = _Checking;
            const MCUpdateWork::MCProgressReport report{.Received = checking ? std::max<uint64_t>(_Received, _InHand)
                                                                             : _Received.load(),
                                                        .Expected = _Expected,
                                                        .Checking = checking};

            if (_Progress && !_Progress(report))
            {
                MarkStopped();
            }

            return !_Stopped;
        }

        /// <summary>On a worker's thread: counts bytes in (staging's thread tells them), and whether to go on.</summary>
        bool Counted(uint64_t count)
        {
            _Received += count;
            return !_Stopped;
        }

        /// <summary>
        /// The count of the download of the hosted file <paramref name="name"/> (MCContentStage::FetchChecked's): its
        /// first bytes, those already here or the first to arrive, end the check of what an earlier attempt left; the
        /// bytes already here as staging began, counted then, aren't counted again. On a worker's thread
        /// (<paramref name="onWorker"/>) it counts as Counted does, else as Received.
        /// </summary>
        std::function<bool(uint64_t)> CountFor(std::string_view name, bool onWorker)
        {
            const auto prior = _Prior.find(name);
            uint64_t counted = prior != _Prior.end() ? prior->second : 0;

            return [this, counted, onWorker](uint64_t count) mutable
            {
                if (count > 0)
                {
                    _Checking = false;
                }

                const uint64_t again = std::min(count, counted);
                counted -= again;
                return onWorker ? Counted(count - again) : Received(count - again);
            };
        }

        /// <summary>Stopped: the workers' fetches are cancelled, so one that's stalled ends at once.</summary>
        void MarkStopped()
        {
            _Stopped = true;
            const std::scoped_lock lock(_WorkersLock);

            for (const std::weak_ptr<MCContentStage::MCSource>& kept : _Workers)
            {
                if (const std::shared_ptr<MCContentStage::MCSource> worker = kept.lock())
                {
                    worker->Cancel();
                }
            }
        }

        /// <summary>Whether staging is to stop: asked before each request, hash, patch and unpack.</summary>
        bool Stopped() { return !Received(0); }

        /// <summary>Ends staging as stopped: what's in stays for the next attempt.</summary>
        std::unexpected<std::string> Stop()
        {
            MarkStopped();
            return std::unexpected(std::string(StoppedError));
        }

        MCUpdateDisk& _Disk;
        MCContentStage::MCSource& _Source;
        std::filesystem::path _Game;
        /// <summary>Each path staging writes or deletes looked at just before (MCNoLinks).</summary>
        MCNoLinks _NoLinks;
        const MCContentPlan::MCPlan& _Plan;
        const MCContentList::MCList& _List;
        const MCUpdateWork::MCProgressCallback& _Progress;
        /// <summary>The bytes in, counted by the workers too; those expected; whether staging is to stop.</summary>
        std::atomic<uint64_t> _Received = 0;
        uint64_t _Expected = 0;
        std::atomic<bool> _Stopped = false;
        /// <summary>
        /// What an earlier attempt left, by its sizes, and whether it's still being checked (told as in hand
        /// meanwhile); the workers' first bytes end the check too.
        /// </summary>
        uint64_t _InHand = 0;
        std::atomic<bool> _Checking = false;
        /// <summary>
        /// The bytes of each hosted file, not yet staged, already downloaded as staging began: counted in then. Made
        /// before the workers start, and only read after.
        /// </summary>
        std::map<std::string, uint64_t, std::less<>> _Prior;
        /// <summary>When the space check refused: the bytes short.</summary>
        std::optional<uint64_t> _Short;
        /// <summary>The workers' sources while they fetch ahead.</summary>
        std::mutex _WorkersLock;
        std::vector<std::weak_ptr<MCContentStage::MCSource>> _Workers;
        /// <summary>Asked in the long loops (a hash's reads, a patch's writes, an entry's): false once stopped.</summary>
        const std::function<bool()> _GoOn = [this] { return !Stopped(); };
    };
}

namespace
{
    /// <summary>
    /// One try at MCContentStage::FetchChecked: the bytes already here hashed, the rest fetched, the whole checked (one
    /// that isn't as named deleted). <paramref name="goOn"/> is told the counts and sets <paramref name="stopped"/>;
    /// <paramref name="gotBytes"/> says whether any bytes came from the source.
    /// </summary>
    std::expected<void, MCContentStage::MCFetchError> FetchCheckedOnce(
        MCUpdateDisk& disk, MCContentStage::MCSource& source, std::string_view name, const std::filesystem::path& to,
        uint64_t size, const MCSha256::MCDigest& sha256, const std::function<bool(uint64_t)>& goOn, const bool& stopped,
        bool& gotBytes)
    {
        using MCKind = MCContentStage::MCFetchError::MCKind;
        using MCFetchError = MCContentStage::MCFetchError;
        MCUpdateDisk::MCStat stat = disk.Stat(to);

        // A folder, or longer than the file: not a cut-off download of it, fetched again.
        if (stat.Exists && (stat.Folder || stat.Size > size))
        {
            if (!disk.Remove(to))
            {
                return std::unexpected(
                    MCFetchError{MCKind::Download, std::format("{} can't be deleted", MCFileSystem::ToUtf8(to))});
            }

            stat = {};
        }

        if (!disk.MakeFolders(to.parent_path()))
        {
            return std::unexpected(MCFetchError{
                MCKind::Download, std::format("{} can't be made", MCFileSystem::ToUtf8(to.parent_path()))});
        }

        // The bytes already here hashed once; those that arrive, as they come.
        MCSha256::MCHasher hasher;
        uint64_t hashed = 0;

        if (stat.Exists)
        {
            const auto added = hasher.AddFile(to, stat.Size, [&] { return goOn(0); });

            if (!added)
            {
                return std::unexpected(stopped ? MCFetchError{MCKind::Stopped, std::string(StoppedError)}
                                               : MCFetchError{MCKind::Download, added.error()});
            }

            hashed = *added;
        }

        if (!goOn(hashed))
        {
            return std::unexpected(MCFetchError{MCKind::Stopped, std::string(StoppedError)});
        }

        if (hashed < size)
        {
            const auto fetched = source.Fetch(name, to, size,
                                              [&](std::span<const char> bytes)
                                              {
                                                  hasher.Add(bytes);
                                                  hashed += bytes.size();
                                                  gotBytes = gotBytes || !bytes.empty();
                                                  return goOn(bytes.size());
                                              });

            if (!fetched)
            {
                if (stopped || fetched.error() == StoppedError)
                {
                    return std::unexpected(MCFetchError{MCKind::Stopped, std::string(StoppedError)});
                }

                return std::unexpected(MCFetchError{MCKind::Download, fetched.error()});
            }
        }

        if (!goOn(0))
        {
            return std::unexpected(MCFetchError{MCKind::Stopped, std::string(StoppedError)});
        }

        if (hashed != size || disk.Stat(to).Size != size || hasher.Finish() != sha256)
        {
            if (!disk.Remove(to))
            {
                SDL_Log("Updates: %s, not as named, can't be deleted", MCFileSystem::ToUtf8(to).c_str());
            }

            return std::unexpected(MCFetchError{MCKind::NotAsNamed, std::format("{} isn't as named", name)});
        }

        return {};
    }
}

std::expected<void, MCContentStage::MCFetchError> MCContentStage::FetchChecked(
    MCUpdateDisk& disk, MCSource& source, std::string_view name, const std::filesystem::path& to, uint64_t size,
    const MCSha256::MCDigest& sha256, const std::function<bool(uint64_t)>& received)
{
    bool stopped = false;
    // The bytes told so far, and those still to come again from another host that were told already (a copy given up
    // was): those aren't told twice, so the count never passes the file's size.
    uint64_t told = 0;
    uint64_t toldAlready = 0;
    const std::function<bool(uint64_t)> goOn = [&](uint64_t count)
    {
        const uint64_t again = std::min(count, toldAlready);
        toldAlready -= again;
        told += count - again;
        stopped = stopped || !received(count - again);
        return !stopped;
    };

    for (;;)
    {
        bool gotBytes = false;
        auto checked = FetchCheckedOnce(disk, source, name, to, size, sha256, goOn, stopped, gotBytes);

        // A copy not as named, deleted, comes again whole from another host while the source has one; one whose bytes
        // were all here before (none fetched now) is as it was: not as named.
        if (checked || checked.error().Kind != MCFetchError::MCKind::NotAsNamed || !gotBytes || disk.Stat(to).Exists ||
            !source.PassOver(name))
        {
            return checked;
        }

        SDL_Log("Updates: %.*s isn't as named: fetched again whole from another host", static_cast<int>(name.size()),
                name.data());
        toldAlready = told;
    }
}

std::vector<MCContentSwap::MCMove> MCContentStage::Moves(const MCContentPlan::MCPlan& plan)
{
    std::vector<MCContentSwap::MCMove> moves;

    for (const MCStep& step : plan.Steps)
    {
        if (step.Action != MCAction::Fetch && step.Action != MCAction::Patch && step.Action != MCAction::Remove)
        {
            continue;
        }

        MCContentSwap::MCMove move{.Path = step.OnDisk, .Keep = step.Keep};

        if (step.Found)
        {
            move.Old = step.Found;
            move.OldSize = step.FoundSize;
            move.OldWritten = step.FoundWritten;
        }

        if (step.Action != MCAction::Remove)
        {
            move.New = step.File->Sha256;
            move.NewSize = step.File->Size;
        }

        moves.push_back(std::move(move));
    }

    return moves;
}

std::expected<void, MCUpdateWork::MCFailure> MCContentStage::Stage(MCUpdateDisk& disk, MCSource& source,
                                                                   const std::filesystem::path& gameFolder,
                                                                   const MCContentPlan::MCPlan& plan,
                                                                   const MCContentList::MCList& list,
                                                                   const MCUpdateWork::MCProgressCallback& progress)
{
    MCStager stager(disk, source, gameFolder, plan, list, progress);
    auto staged = stager.Run();

    if (staged)
    {
        return {};
    }

    using MCUpdateWork::MCFailureKind;

    if (const std::optional<uint64_t> bytesShort = stager.BytesShort())
    {
        return std::unexpected(MCUpdateWork::MCFailure{
            .Kind = MCFailureKind::DiskFull, .Reason = std::move(staged.error()), .BytesShort = *bytesShort});
    }

    return std::unexpected(
        MCUpdateWork::MCFailure{.Kind = source.HostUnreachable() ? MCFailureKind::Unreachable : MCFailureKind::Other,
                                .Reason = std::move(staged.error())});
}

std::expected<void, std::string> MCContentStage::Arm(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                     const MCContentPlan::MCPlan& plan)
{
    if (!MCContentSwap::MarkReady(disk, gameFolder, {Moves(plan), plan.Record, plan.LowestProgram}))
    {
        // A write that failed only at its last flush left the file: it goes, or the next start would swap in content
        // whose program was never armed.
        const std::filesystem::path ready = gameFolder / MCFileSystem::FromUtf8(MCContentSwap::ReadyFile);

        if (disk.Stat(ready).Exists &&
            (!disk.Remove(ready) ||
             !disk.FlushFolder(gameFolder / MCFileSystem::FromUtf8(MCContentSwap::UpdateFolder))))
        {
            SDL_Log("Updates: %s can't be taken back after a failed write", MCFileSystem::ToUtf8(ready).c_str());
        }

        return std::unexpected("the update can't be marked ready");
    }

    return {};
}
