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
    /// Where an entry is unpacked to, its bytes and hash as they're written, whether writing failed (the disk's fault,
    /// not the zip's), and whether it was stopped (asked before each piece).
    /// </summary>
    struct MCEntrySink
    {
        explicit MCEntrySink(const std::function<bool()>& goOn) : GoOn(goOn) {}

        std::ofstream Out;
        MCSha256::MCHasher Hasher;
        uint64_t Written = 0;
        const std::function<bool()>& GoOn;
        bool WriteFailed = false;
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

        const std::span<const char> bytes(static_cast<const char*>(buffer), count);
        sink.Out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        sink.WriteFailed = sink.WriteFailed || !sink.Out;

        if (sink.WriteFailed)
        {
            return 0;
        }

        sink.Hasher.Add(bytes);
        sink.Written += bytes.size();
        return count;
    }

    class MCStager
    {
    public:
        MCStager(MCUpdateDisk& disk, MCContentStage::MCSource& source, std::filesystem::path game,
                 const MCContentPlan::MCPlan& plan, const MCContentList::MCList& list,
                 const std::function<bool(uint64_t, uint64_t)>& progress)
            : _Disk(disk)
            , _Source(source)
            , _Game(std::move(game))
            , _Plan(plan)
            , _List(list)
            , _Progress(progress)
            , _Expected(plan.DownloadBytes)
        {
        }

        std::expected<void, std::string> Run()
        {
            // Staging over a ready update would change files its journal moves: that update swaps in first.
            if (MCContentSwap::Waiting(_Disk, _Game))
            {
                return std::unexpected("an update is already staged; it goes in at the next start");
            }

            if (auto cleared = ClearUnplanned(); !cleared)
            {
                return cleared;
            }

            // What an earlier attempt staged as the list has it stays; the rest is still to do.
            std::vector<const MCStep*> patches;
            std::vector<const MCStep*> whole;

            for (const MCStep& step : _Plan.Steps)
            {
                if (step.Action != MCAction::Patch && step.Action != MCAction::Fetch)
                {
                    continue;
                }

                if (!IsStaged(step))
                {
                    (step.Action == MCAction::Patch ? patches : whole).push_back(&step);
                }
                else if (step.Action == MCAction::Patch)
                {
                    // Staged by an earlier attempt: its patch counts as in.
                    Received(step.Patch->Size);
                }
            }

            if (Stopped())
            {
                return Stop();
            }

            const std::set<const MCContentList::MCZip*> zipsPlanned = ZipsHolding(whole);

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
                std::vector<const MCStep*> entries;
                std::ranges::copy_if(whole, std::back_inserter(entries),
                                     [&](const MCStep* step) { return step->File->Zip == zip.Name; });

                if (entries.empty())
                {
                    // Every file it holds staged by an earlier attempt: it counts as in.
                    if (std::ranges::find(_Plan.Zips, &zip) != _Plan.Zips.end())
                    {
                        Received(zip.Size);
                    }

                    continue;
                }

                // A zip the plan didn't count (a patch fell back on it) adds to what's downloaded.
                if (std::ranges::find(_Plan.Zips, &zip) == _Plan.Zips.end())
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
        /// Whether the step's file is staged as the list has it: found staged by an earlier attempt, or just patched.
        /// </summary>
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

                if (step.Patch != nullptr)
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

        /// <summary>The zips holding <paramref name="steps"/>' files.</summary>
        std::set<const MCContentList::MCZip*> ZipsHolding(const std::vector<const MCStep*>& steps) const
        {
            std::set<const MCContentList::MCZip*> zips;

            for (const MCContentList::MCZip& zip : _List.Zips)
            {
                if (std::ranges::any_of(steps, [&](const MCStep* step) { return step->File->Zip == zip.Name; }))
                {
                    zips.insert(&zip);
                }
            }

            return zips;
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
        std::expected<void, std::string> CheckSpace(const std::vector<const MCStep*>& patches,
                                                    const std::vector<const MCStep*>& whole)
        {
            uint64_t needed = 0;
            uint64_t biggestZip = 0;
            std::vector<uint64_t> patchesLeft;

            for (const MCStep* step : patches)
            {
                needed += step->File->Size;
                patchesLeft.push_back(step->Patch->Size - Downloaded(step->Patch->Name, step->Patch->Size));
            }

            const size_t ahead = std::min(PatchWorkers, patchesLeft.size());
            std::ranges::partial_sort(patchesLeft, patchesLeft.begin() + static_cast<std::ptrdiff_t>(ahead),
                                      std::greater<>());
            needed =
                std::accumulate(patchesLeft.begin(), patchesLeft.begin() + static_cast<std::ptrdiff_t>(ahead), needed);

            for (const MCStep* step : whole)
            {
                needed += step->File->Size;
            }

            for (const MCContentList::MCZip* zip : ZipsHolding(whole))
            {
                biggestZip = std::max(biggestZip, zip->Size - Downloaded(zip->Name, zip->Size));
            }

            needed += biggestZip;
            const auto free = _Disk.FreeBytes(_Game);

            if (!free)
            {
                SDL_Log("Updates: staging goes on unchecked: %s", free.error().c_str());
                return {};
            }

            if (*free < needed)
            {
                return std::unexpected(std::format(
                    "the update needs {:.1f} GB free on the game's disk; it has {:.1f} GB",
                    static_cast<double>(needed) / BytesPerGigabyte, static_cast<double>(*free) / BytesPerGigabyte));
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
        /// Stages <paramref name="patches"/>' files, each patch applied in order; one whose patch doesn't come or
        /// doesn't apply joins <paramref name="whole"/>. With a source that makes others, the patches are fetched
        /// ahead by workers, <see cref="PatchWorkers"/> at once; otherwise one by one.
        /// </summary>
        /// <returns>Nothing, or stopped.</returns>
        std::expected<void, std::string> PatchAll(const std::vector<const MCStep*>& patches,
                                                  std::vector<const MCStep*>& whole)
        {
            std::vector<std::unique_ptr<MCContentStage::MCSource>> sources;

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
                for (const MCStep* step : patches)
                {
                    const auto fetched = FetchPatch(_Source, *step, [this](uint64_t count) { return Received(count); });

                    if (auto patched = Patched(*step, fetched, whole); !patched)
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

        std::expected<void, std::string> PatchAhead(const std::vector<const MCStep*>& patches,
                                                    std::vector<std::unique_ptr<MCContentStage::MCSource>>& sources,
                                                    std::vector<const MCStep*>& whole)
        {
            MCAhead ahead;
            ahead.Fetched.resize(patches.size());
            MCWorkers workers(*this, ahead);

            {
                // A stop cancels them, so a fetch that's stalled ends at once.
                const std::scoped_lock lock(_WorkersLock);

                for (const std::unique_ptr<MCContentStage::MCSource>& source : sources)
                {
                    _Workers.push_back(source.get());
                }
            }

            if (_Stopped)
            {
                return Stop();
            }

            for (const std::unique_ptr<MCContentStage::MCSource>& source : sources)
            {
                workers.Threads.emplace_back([this, &ahead, &patches, worker = source.get()]
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

                auto patched = Patched(*patches[index], fetched, whole);

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
        void FetchAhead(MCContentStage::MCSource& source, MCAhead& ahead, const std::vector<const MCStep*>& patches)
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

                auto fetched = FetchPatch(source, *patches[index], [this](uint64_t count) { return Counted(count); });

                {
                    const std::scoped_lock lock(ahead.Lock);
                    ahead.Fetched[index] = std::move(fetched);
                }

                ahead.Changed.notify_all();
            }
        }

        /// <summary>
        /// Applies a patch fetched; one that didn't come, or doesn't apply, leaves its file to fetch whole (<paramref
        /// name="whole"/>).
        /// </summary>
        /// <returns>Nothing, or stopped: a stop isn't a patch that failed, so nothing more is fetched or applied.</returns>
        std::expected<void, std::string> Patched(const MCStep& step,
                                                 const std::expected<std::filesystem::path, std::string>& fetched,
                                                 std::vector<const MCStep*>& whole)
        {
            std::expected<void, std::string> patched = std::unexpected(fetched.error_or(std::string()));

            if (fetched)
            {
                patched = Apply(step, *fetched);
            }

            if (patched)
            {
                return {};
            }

            if (Stopped())
            {
                return Stop();
            }

            SDL_Log("Updates: %s is fetched whole: %s", step.Path.c_str(), patched.error().c_str());
            whole.push_back(&step);
            return {};
        }

        /// <summary>Downloads a step's patch into download/, checked.</summary>
        std::expected<std::filesystem::path, std::string> FetchPatch(MCContentStage::MCSource& source,
                                                                     const MCStep& step,
                                                                     const std::function<bool(uint64_t)>& received)
        {
            return FetchChecked(source, step.Patch->Name, step.Patch->Size, step.Patch->Sha256, received);
        }

        /// <summary>Writes the patched file to staged/; fails (and leaves nothing) when it can't.</summary>
        std::expected<void, std::string> Apply(const MCStep& step, const std::filesystem::path& patch)
        {
            const std::filesystem::path staged = StagedPath(step);

            if (!_Disk.MakeFolders(staged.parent_path()))
            {
                return std::unexpected(std::format("{} can't be made", MCFileSystem::ToUtf8(staged.parent_path())));
            }

            if (Stopped())
            {
                return Stop();
            }

            const auto applied = MCFilePatch::Apply(At(step.OnDisk), patch, staged, _GoOn);

            // Stopped, the patch stays for the next attempt.
            if (Stopped())
            {
                return Stop();
            }

            _Disk.Remove(patch);

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
        std::expected<void, std::string> FetchAndUnpack(const MCContentList::MCZip& zip,
                                                        const std::vector<const MCStep*>& entries)
        {
            const auto path = FetchChecked(_Source, zip.Name, zip.Size, zip.Sha256,
                                           [this](uint64_t count) { return Received(count); });

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

            for (const MCStep* step : entries)
            {
                // Stopped, the zip stays for the next attempt.
                if (Stopped())
                {
                    error = StoppedError;
                    keepZip = true;
                    break;
                }

                const int index =
                    mz_zip_reader_locate_file(&archive, step->File->Path.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
                const std::filesystem::path staged = StagedPath(*step);

                if (index < 0)
                {
                    error = std::format("{} isn't in {}", step->File->Path, zip.Name);
                    break;
                }

                if (!_Disk.MakeFolders(staged.parent_path()))
                {
                    error = std::format("{} can't be made", MCFileSystem::ToUtf8(staged.parent_path()));
                    keepZip = true;
                    break;
                }

                MCEntrySink sink(_GoOn);
                sink.Out.open(staged, std::ios::binary | std::ios::trunc);
                const bool unpacked = sink.Out && mz_zip_reader_extract_to_callback(
                                                      &archive, static_cast<mz_uint>(index), WriteEntry, &sink, 0);
                sink.Out.close();

                if (sink.Stopped)
                {
                    _Disk.Remove(staged);
                    error = StoppedError;
                    keepZip = true;
                    break;
                }

                if (!sink.Out || sink.WriteFailed)
                {
                    _Disk.Remove(staged);
                    error = std::format("{} can't be written", MCFileSystem::ToUtf8(staged));
                    keepZip = true;
                    break;
                }

                if (!unpacked || sink.Written != step->File->Size || sink.Hasher.Finish() != step->File->Sha256)
                {
                    _Disk.Remove(staged);
                    error = std::format("{} from {} isn't the release's", step->File->Path, zip.Name);
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

        /// <summary>On staging's own thread: counts bytes in, tells the progress, and asks whether to go on.</summary>
        bool Received(uint64_t count)
        {
            _Received += count;

            if (_Progress && !_Progress(_Received, _Expected))
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

        /// <summary>Stopped: the workers' fetches are cancelled, so one that's stalled ends at once.</summary>
        void MarkStopped()
        {
            _Stopped = true;
            const std::scoped_lock lock(_WorkersLock);

            for (MCContentStage::MCSource* worker : _Workers)
            {
                worker->Cancel();
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
        const MCContentPlan::MCPlan& _Plan;
        const MCContentList::MCList& _List;
        const std::function<bool(uint64_t, uint64_t)>& _Progress;
        /// <summary>The bytes in, counted by the workers too; those expected; whether staging is to stop.</summary>
        std::atomic<uint64_t> _Received = 0;
        uint64_t _Expected = 0;
        std::atomic<bool> _Stopped = false;
        /// <summary>The workers' sources while they fetch ahead.</summary>
        std::mutex _WorkersLock;
        std::vector<MCContentStage::MCSource*> _Workers;
        /// <summary>Asked in the long loops (a hash's reads, a patch's writes, an entry's): false once stopped.</summary>
        const std::function<bool()> _GoOn = [this] { return !Stopped(); };
    };
}

std::expected<void, MCContentStage::MCFetchError> MCContentStage::FetchChecked(
    MCUpdateDisk& disk, MCSource& source, std::string_view name, const std::filesystem::path& to, uint64_t size,
    const MCSha256::MCDigest& sha256, const std::function<bool(uint64_t)>& received)
{
    using MCKind = MCFetchError::MCKind;
    bool stopped = false;
    const auto goOn = [&](uint64_t count)
    {
        stopped = stopped || !received(count);
        return !stopped;
    };

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
        return std::unexpected(
            MCFetchError{MCKind::Download, std::format("{} can't be made", MCFileSystem::ToUtf8(to.parent_path()))});
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

std::expected<void, std::string> MCContentStage::Stage(MCUpdateDisk& disk, MCSource& source,
                                                       const std::filesystem::path& gameFolder,
                                                       const MCContentPlan::MCPlan& plan,
                                                       const MCContentList::MCList& list,
                                                       const std::function<bool(uint64_t, uint64_t)>& progress)
{
    return MCStager(disk, source, gameFolder, plan, list, progress).Run();
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
