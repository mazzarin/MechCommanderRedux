#include "stdafx.h"
#include "update/MCContentStage.h"
#include "lib/MCFilePatch.h"
#include "platform/MCFileSystem.h"
#include "update/MCUpdateDisk.h"

// miniz is built without stdio or time (MCCore.vcxproj, src/mac/CMakeLists.txt); its header is read the same.
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "miniz.h"

namespace
{
    using MCContentPlan::MCAction;
    using MCContentPlan::MCStep;

    constexpr double BytesPerGigabyte = 1e9;
    /// <summary>What a stop ends staging with, as a source's Cancel ends its fetch (MCSource::Cancel).</summary>
    constexpr std::string_view StoppedError = "stopped";

    std::string Shown(const std::filesystem::path& path)
    {
        return MCFileSystem::ToUtf8(path);
    }

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
    /// Where an entry is unpacked to, whether writing it failed (the disk's fault, not the zip's), and whether it was
    /// stopped (asked before each piece).
    /// </summary>
    struct MCEntrySink
    {
        explicit MCEntrySink(const std::function<bool()>& goOn) : GoOn(goOn) {}

        std::ofstream Out;
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

        sink.Out.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(count));
        sink.WriteFailed = sink.WriteFailed || !sink.Out;
        return sink.WriteFailed ? 0 : count;
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

            for (const MCStep* step : patches)
            {
                if (const auto patched = Patch(*step); !patched)
                {
                    // A stop isn't a patch that failed: nothing more is fetched or applied.
                    if (Stopped())
                    {
                        return Stop();
                    }

                    SDL_Log("Updates: %s is fetched whole: %s", step->Path.c_str(), patched.error().c_str());
                    whole.push_back(step);
                }
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

            return Stopped() ? Stop() : Flush();
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

        /// <summary>Whether the step's file is already staged as the list has it (an earlier attempt's).</summary>
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
        /// name="whole"/>'s files), the biggest patch still to fetch and the biggest zip; what's already staged or
        /// downloaded takes its room already. When the free space can't be read, it's logged and staging goes on.
        /// </summary>
        std::expected<void, std::string> CheckSpace(const std::vector<const MCStep*>& patches,
                                                    const std::vector<const MCStep*>& whole)
        {
            uint64_t needed = 0;
            uint64_t biggestPatch = 0;
            uint64_t biggestZip = 0;

            for (const MCStep* step : patches)
            {
                needed += step->File->Size;
                biggestPatch =
                    std::max(biggestPatch, step->Patch->Size - Downloaded(step->Patch->Name, step->Patch->Size));
            }

            for (const MCStep* step : whole)
            {
                needed += step->File->Size;
            }

            for (const MCContentList::MCZip* zip : ZipsHolding(whole))
            {
                biggestZip = std::max(biggestZip, zip->Size - Downloaded(zip->Name, zip->Size));
            }

            needed += biggestPatch + biggestZip;
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

        /// <summary>Downloads a hosted file (resuming), and checks it's the list's; a stop ends it at once.</summary>
        std::expected<std::filesystem::path, std::string> FetchChecked(std::string_view name, uint64_t size,
                                                                       const MCSha256::MCDigest& sha256)
        {
            const std::filesystem::path path = DownloadPath(name);
            MCUpdateDisk::MCStat stat = _Disk.Stat(path);

            if (stat.Exists && (stat.Folder || stat.Size > size))
            {
                _Disk.Remove(path);
                stat = {};
            }

            if (!_Disk.MakeFolders(path.parent_path()))
            {
                return std::unexpected(std::format("{} can't be made", Shown(path.parent_path())));
            }

            if (!Received(stat.Size))
            {
                return Stop();
            }

            if (stat.Size < size)
            {
                const auto fetched = _Source.Fetch(name, path, size, [&](uint64_t count) { return Received(count); });

                if (!fetched)
                {
                    if (Stopped() || fetched.error() == StoppedError)
                    {
                        return Stop();
                    }

                    return std::unexpected(std::format("{} didn't download: {}", name, fetched.error()));
                }
            }

            if (Stopped())
            {
                return Stop();
            }

            const auto hash = MCSha256::OfFile(path, _GoOn);

            if (Stopped())
            {
                return Stop();
            }

            if (_Disk.Stat(path).Size != size || !hash || *hash != sha256)
            {
                _Disk.Remove(path);
                return std::unexpected(std::format("{} isn't as the list has it", name));
            }

            return path;
        }

        /// <summary>Writes the patched file to staged/; fails (and leaves nothing) when it can't.</summary>
        std::expected<void, std::string> Patch(const MCStep& step)
        {
            const auto patch = FetchChecked(step.Patch->Name, step.Patch->Size, step.Patch->Sha256);

            if (!patch)
            {
                return std::unexpected(patch.error());
            }

            const std::filesystem::path staged = StagedPath(step);

            if (!_Disk.MakeFolders(staged.parent_path()))
            {
                return std::unexpected(std::format("{} can't be made", Shown(staged.parent_path())));
            }

            if (Stopped())
            {
                return Stop();
            }

            const auto applied = MCFilePatch::Apply(At(step.OnDisk), *patch, staged, _GoOn);

            // Stopped, the patch stays for the next attempt.
            if (Stopped())
            {
                return Stop();
            }

            _Disk.Remove(*patch);

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
            const auto path = FetchChecked(zip.Name, zip.Size, zip.Sha256);

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
                    error = std::format("{} can't be made", Shown(staged.parent_path()));
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
                    error = std::format("{} can't be written", Shown(staged));
                    keepZip = true;
                    break;
                }

                if (!unpacked || !IsStaged(*step))
                {
                    if (Stopped())
                    {
                        error = StoppedError;
                        keepZip = true;
                        break;
                    }

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

        /// <summary>Flushes every staged file and the folders holding them; the downloads go.</summary>
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

                if (!_Disk.FlushFile(staged))
                {
                    return std::unexpected(std::format("{} can't be flushed", Shown(staged)));
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
                if (!_Disk.FlushFolder(folder))
                {
                    return std::unexpected(std::format("{} can't be flushed", Shown(folder)));
                }
            }

            _Disk.Remove(At(MCContentStage::DownloadFolder));
            return {};
        }

        /// <summary>Counts bytes in, and asks whether to go on.</summary>
        bool Received(uint64_t count)
        {
            _Received += count;

            if (_Progress && !_Progress(_Received, _Expected))
            {
                _Stopped = true;
            }

            return !_Stopped;
        }

        /// <summary>Whether staging is to stop: asked before each request, hash, patch and unpack.</summary>
        bool Stopped() { return !Received(0); }

        /// <summary>Ends staging as stopped: what's in stays for the next attempt.</summary>
        std::unexpected<std::string> Stop()
        {
            _Stopped = true;
            return std::unexpected(std::string(StoppedError));
        }

        MCUpdateDisk& _Disk;
        MCContentStage::MCSource& _Source;
        std::filesystem::path _Game;
        const MCContentPlan::MCPlan& _Plan;
        const MCContentList::MCList& _List;
        const std::function<bool(uint64_t, uint64_t)>& _Progress;
        uint64_t _Received = 0;
        uint64_t _Expected = 0;
        bool _Stopped = false;
        /// <summary>Asked in the long loops (a hash's reads, a patch's writes, an entry's): false once stopped.</summary>
        const std::function<bool()> _GoOn = [this] { return !Stopped(); };
    };
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
    if (!MCContentSwap::MarkReady(disk, gameFolder, {Moves(plan), plan.Record}))
    {
        // A write that failed only at its last flush left the file: it goes, or the next start would swap in content
        // whose program was never armed.
        const std::filesystem::path ready = gameFolder / MCFileSystem::FromUtf8(MCContentSwap::ReadyFile);

        if (disk.Stat(ready).Exists &&
            (!disk.Remove(ready) ||
             !disk.FlushFolder(gameFolder / MCFileSystem::FromUtf8(MCContentSwap::UpdateFolder))))
        {
            SDL_Log("Updates: %s can't be taken back after a failed write", Shown(ready).c_str());
        }

        return std::unexpected("the update can't be marked ready");
    }

    return {};
}
