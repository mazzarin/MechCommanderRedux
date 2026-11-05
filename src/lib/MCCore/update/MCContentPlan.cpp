#include "stdafx.h"
#include "update/MCContentPlan.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"

namespace
{
    using namespace MCContentPlan;
    using MCContentList::FoldedPath;

    /// <summary>A file on disk as it was found.</summary>
    struct MCHashed
    {
        MCSha256::MCDigest Sha256{};
        uint64_t Size = 0;
        int64_t Written = 0;
    };

    /// <summary>
    /// The file at <paramref name="onDisk"/>: its recorded hash when its size and time are as <paramref name="had"/>
    /// recorded them, else hashed now.
    /// </summary>
    std::expected<MCHashed, std::string> Hashed(const MCContentFolder& folder, const std::string& onDisk,
                                                const MCInstalled::MCFile* had)
    {
        const std::filesystem::path path = folder.Full(onDisk);
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);
        const std::optional<int64_t> written = MCInstalled::WrittenAt(path);

        if (error || !written)
        {
            return std::unexpected(std::format("{} can't be looked at", MCFileSystem::ToUtf8(path)));
        }

        if (had != nullptr && had->Path == onDisk && had->Size == size && had->Written == *written && had->Written != 0)
        {
            return MCHashed{had->Sha256, size, *written};
        }

        const auto digest = MCSha256::OfFile(path);

        if (!digest)
        {
            return std::unexpected(digest.error());
        }

        return MCHashed{*digest, size, *written};
    }

    /// <summary>Whether <paramref name="hash"/> is one of <paramref name="file"/>'s official versions.</summary>
    bool IsOfficial(const MCContentList::MCFile& file, const MCSha256::MCDigest& hash, const MCInstalled::MCFile* had)
    {
        if (had != nullptr && had->Ours && had->Sha256 == hash)
        {
            return true;
        }

        return std::ranges::find(file.Earlier, hash) != file.Earlier.end() ||
               std::ranges::any_of(file.Patches,
                                   [&](const MCContentList::MCPatch& patch) { return patch.From == hash; });
    }

    const MCInstalled::MCFile* Recorded(const MCInstalled::MCRecord& installed, std::string_view folded)
    {
        const auto found = installed.Files.find(folded);
        return found == installed.Files.end() ? nullptr : &found->second;
    }

    /// <summary>Why a file can't be planned when something not a file is in its place.</summary>
    std::string InTheWay(std::string_view listed, const MCContentFolder::MCFound& found)
    {
        return found.Kind == MCContentFolder::MCKind::Link
                   ? std::format("{}: {} is a link, which updates don't follow", listed, found.Path)
                   : std::format("{}: {} is in its way", listed, found.Path);
    }
}

bool MCContentPlan::MCPlan::Changes() const
{
    return std::ranges::any_of(Steps,
                               [](const MCStep& step)
                               {
                                   return step.Action == MCAction::Fetch || step.Action == MCAction::Patch ||
                                          step.Action == MCAction::Remove;
                               });
}

std::expected<MCPlan, std::string> MCContentPlan::Make(MCContentFolder& folder, const MCInstalled::MCRecord& installed,
                                                       const MCContentList::MCList& list,
                                                       const MCSha256::MCDigest& listHash,
                                                       const std::set<std::string, std::less<>>& sets)
{
    MCPlan plan;
    plan.Record.Release = list.Release;
    plan.Record.List = listHash;
    plan.Record.Sets = sets;
    std::set<std::string, std::less<>> zips;
    std::set<std::string, std::less<>> listed;

    for (const MCContentList::MCFile& file : list.Files)
    {
        if (!sets.contains(file.Set))
        {
            continue;
        }

        const std::string folded = FoldedPath(file.Path);
        listed.insert(folded);
        const MCInstalled::MCFile* const had = Recorded(installed, folded);
        const MCContentFolder::MCFound found = folder.Find(file.Path);
        MCStep step;
        step.Path = file.Path;
        step.File = &file;
        MCInstalled::MCFile record{.Path = {}, .Set = file.Set, .Sha256 = file.Sha256, .Size = file.Size};

        switch (found.Kind)
        {
            case MCContentFolder::MCKind::Link:
            case MCContentFolder::MCKind::Blocked:
                return std::unexpected(InTheWay(file.Path, found));
            case MCContentFolder::MCKind::Missing:
            {
                step.Action = MCAction::Fetch;
                step.OnDisk = folder.Place(file.Path);
                break;
            }
            case MCContentFolder::MCKind::File:
            {
                step.OnDisk = found.Path;
                const auto hashed = Hashed(folder, found.Path, had);

                if (!hashed)
                {
                    return std::unexpected(hashed.error());
                }

                step.Found = hashed->Sha256;

                if (hashed->Sha256 == file.Sha256)
                {
                    step.Action = MCAction::Keep;
                    record.Written = hashed->Written;
                }
                else if (IsOfficial(file, hashed->Sha256, had))
                {
                    const auto patch = std::ranges::find(file.Patches, hashed->Sha256, &MCContentList::MCPatch::From);
                    step.Action = patch != file.Patches.end() ? MCAction::Patch : MCAction::Fetch;
                    step.Patch = patch != file.Patches.end() ? &*patch : nullptr;
                }
                else
                {
                    step.Action = MCAction::LeaveTheirs;
                    record.Sha256 = hashed->Sha256;
                    record.Size = hashed->Size;
                    record.Written = hashed->Written;
                    record.Ours = false;
                }

                break;
            }
        }

        if (step.Action == MCAction::Fetch)
        {
            zips.insert(file.Zip);
            plan.StagedBytes += file.Size;
        }
        else if (step.Action == MCAction::Patch)
        {
            plan.DownloadBytes += step.Patch->Size;
            plan.StagedBytes += file.Size;
        }

        record.Path = step.OnDisk;
        plan.Record.Files.insert_or_assign(folded, std::move(record));
        plan.Steps.push_back(std::move(step));
    }

    for (const auto& [folded, had] : installed.Files)
    {
        if (listed.contains(folded) || !had.Ours)
        {
            continue;
        }

        const MCContentFolder::MCFound found = folder.Find(had.Path);

        if (found.Kind != MCContentFolder::MCKind::File)
        {
            continue;
        }

        const auto hashed = Hashed(folder, found.Path, &had);

        if (!hashed)
        {
            return std::unexpected(hashed.error());
        }

        if (hashed->Sha256 == had.Sha256)
        {
            plan.Steps.push_back(MCStep{.Action = MCAction::Remove,
                                        .Path = had.Path,
                                        .OnDisk = found.Path,
                                        .File = nullptr,
                                        .Patch = nullptr,
                                        .Found = hashed->Sha256});
        }
    }

    for (const MCContentList::MCZip& zip : list.Zips)
    {
        if (zips.contains(zip.Name))
        {
            plan.Zips.push_back(&zip);
            plan.DownloadBytes += zip.Size;
        }
    }

    return plan;
}
