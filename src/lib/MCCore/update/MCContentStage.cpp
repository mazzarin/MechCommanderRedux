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

    /// <summary>miniz writes an entry, in order, through this.</summary>
    size_t WriteEntry(void* opaque, mz_uint64, const void* buffer, size_t count)
    {
        auto& file = *static_cast<std::ofstream*>(opaque);
        file.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(count));
        return file ? count : 0;
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
            if (auto space = CheckSpace(); !space)
            {
                return space;
            }

            std::vector<const MCStep*> whole;

            for (const MCStep& step : _Plan.Steps)
            {
                if (step.Action == MCAction::Patch && !IsStaged(step))
                {
                    if (const auto patched = Patch(step); !patched)
                    {
                        SDL_Log("Updates: %s is fetched whole: %s", step.Path.c_str(), patched.error().c_str());
                        whole.push_back(&step);
                    }
                }
                else if (step.Action == MCAction::Fetch && !IsStaged(step))
                {
                    whole.push_back(&step);
                }
            }

            if (_Stopped)
            {
                return std::unexpected("stopped");
            }

            for (const MCContentList::MCZip& zip : _List.Zips)
            {
                std::vector<const MCStep*> entries;
                std::ranges::copy_if(whole, std::back_inserter(entries),
                                     [&](const MCStep* step) { return step->File->Zip == zip.Name; });

                if (entries.empty())
                {
                    continue;
                }

                // A zip the plan didn't count (a patch fell back on it) adds to what's downloaded.
                if (std::ranges::find(_Plan.Zips, &zip) == _Plan.Zips.end())
                {
                    _Expected += zip.Size;
                }

                if (const auto unpacked = FetchAndUnpack(zip, entries); !unpacked)
                {
                    return unpacked;
                }
            }

            return MarkReady();
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

            const auto hash = _Disk.Hash(StagedPath(step));
            return hash && *hash == step.File->Sha256;
        }

        /// <summary>Refuses to start without room for the staged files, the biggest zip and the biggest patch.</summary>
        std::expected<void, std::string> CheckSpace()
        {
            uint64_t biggestZip = 0;
            uint64_t biggestPatch = 0;

            for (const MCContentList::MCZip* zip : _Plan.Zips)
            {
                biggestZip = std::max(biggestZip, zip->Size);
            }

            for (const MCStep& step : _Plan.Steps)
            {
                if (step.Patch != nullptr)
                {
                    biggestPatch = std::max(biggestPatch, step.Patch->Size);
                }
            }

            const uint64_t needed = _Plan.StagedBytes + biggestZip + biggestPatch;
            std::error_code error;
            const std::filesystem::space_info space = std::filesystem::space(_Game, error);

            if (!error && space.available < needed)
            {
                return std::unexpected(
                    std::format("the update needs {:.1f} GB free on the game's disk; it has {:.1f} GB",
                                static_cast<double>(needed) / BytesPerGigabyte,
                                static_cast<double>(space.available) / BytesPerGigabyte));
            }

            return {};
        }

        /// <summary>Downloads a hosted file (resuming), and checks it's the list's.</summary>
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

            Received(stat.Size);

            if (stat.Size < size)
            {
                const auto fetched = _Source.Fetch(name, path, size, [&](uint64_t count) { return Received(count); });

                if (!fetched)
                {
                    return std::unexpected(std::format("{} didn't download: {}", name, fetched.error()));
                }
            }

            const auto hash = _Disk.Hash(path);

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

            const auto applied = MCFilePatch::Apply(At(step.OnDisk), *patch, staged);
            _Disk.Remove(*patch);

            if (!applied)
            {
                return std::unexpected(applied.error());
            }

            if (!IsStaged(step))
            {
                _Disk.Remove(staged);
                return std::unexpected("the patched file isn't the release's");
            }

            return {};
        }

        /// <summary>Downloads a zip and unpacks the entries wanted from it into staged/, each checked.</summary>
        std::expected<void, std::string> FetchAndUnpack(const MCContentList::MCZip& zip,
                                                        const std::vector<const MCStep*>& entries)
        {
            const auto path = FetchChecked(zip.Name, zip.Size, zip.Sha256);

            if (!path)
            {
                return std::unexpected(path.error());
            }

            std::ifstream file(*path, std::ios::binary);
            mz_zip_archive archive{};
            archive.m_pRead = ReadZip;
            archive.m_pIO_opaque = &file;

            if (!file || !mz_zip_reader_init(&archive, zip.Size, 0))
            {
                return std::unexpected(std::format("{} can't be opened as a zip", zip.Name));
            }

            std::string error;

            for (const MCStep* step : entries)
            {
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
                    break;
                }

                bool written = false;
                {
                    std::ofstream out(staged, std::ios::binary | std::ios::trunc);
                    written = out && mz_zip_reader_extract_to_callback(&archive, static_cast<mz_uint>(index),
                                                                       WriteEntry, &out, 0);
                    out.close();
                    written = written && out;
                }

                if (!written || !IsStaged(*step))
                {
                    _Disk.Remove(staged);
                    error = std::format("{} from {} isn't the release's", step->File->Path, zip.Name);
                    break;
                }
            }

            mz_zip_reader_end(&archive);
            file.close();
            _Disk.Remove(*path);

            if (!error.empty())
            {
                return std::unexpected(error);
            }

            return {};
        }

        /// <summary>Flushes every staged file and the folders holding them, then writes the journal-to-be.</summary>
        std::expected<void, std::string> MarkReady()
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

            if (!MCContentSwap::MarkReady(_Disk, _Game, {MCContentStage::Moves(_Plan), _Plan.Record}))
            {
                return std::unexpected("the update can't be marked ready");
            }

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

        MCUpdateDisk& _Disk;
        MCContentStage::MCSource& _Source;
        std::filesystem::path _Game;
        const MCContentPlan::MCPlan& _Plan;
        const MCContentList::MCList& _List;
        const std::function<bool(uint64_t, uint64_t)>& _Progress;
        uint64_t _Received = 0;
        uint64_t _Expected = 0;
        bool _Stopped = false;
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

        MCContentSwap::MCMove move{.Path = step.OnDisk};

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
