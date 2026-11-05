#include "stdafx.h"
#include "update/MCContentPlan.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"
#include "update/MCReleaseFile.h"
#include "update/MCUpdateJson.h"

namespace
{
    using namespace MCContentPlan;
    using MCContentList::FoldedPath;

    /// <summary>The hashes' file: its format, and each file's fields.</summary>
    constexpr std::string_view FormatKey = "format";
    constexpr std::string_view FilesKey = "files";
    constexpr std::string_view PathKey = "path";
    constexpr std::string_view SizeKey = "size";
    constexpr std::string_view WrittenKey = "written";
    constexpr std::string_view Sha256Key = "sha256";
    constexpr uint64_t HashesFormat = 1;

    /// <summary>
    /// Whether a file kept at <paramref name="keptSize"/> and <paramref name="keptWritten"/> is as found, at
    /// <paramref name="size"/> and <paramref name="written"/> (a time of 0 is never trusted).
    /// </summary>
    bool AsFound(uint64_t keptSize, int64_t keptWritten, uint64_t size, int64_t written)
    {
        return keptSize == size && keptWritten == written && keptWritten != 0;
    }

    /// <summary>
    /// The file at <paramref name="onDisk"/>: its recorded hash when its size and time are as <paramref name="had"/>
    /// recorded them, else the one <paramref name="hashes"/> kept when they're as it found them, else hashed now (counted
    /// in <paramref name="plan"/>). One the record doesn't vouch for is kept in the plan's hashes.
    /// </summary>
    std::expected<MCHash, std::string> Hashed(const MCContentFolder& folder, const std::string& onDisk,
                                              const MCInstalled::MCFile* had, const MCHashes& hashes, MCPlan& plan)
    {
        const std::filesystem::path path = folder.Full(onDisk);
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);
        const std::optional<int64_t> written = MCInstalled::WrittenAt(path);

        if (error || !written)
        {
            return std::unexpected(std::format("{} can't be looked at", MCFileSystem::ToUtf8(path)));
        }

        if (had != nullptr && had->Path == onDisk && AsFound(had->Size, had->Written, size, *written))
        {
            return MCHash{size, *written, had->Sha256};
        }

        MCHash found{.Size = size, .Written = *written};

        if (const auto kept = hashes.find(onDisk);
            kept != hashes.end() && AsFound(kept->second.Size, kept->second.Written, size, *written))
        {
            found.Sha256 = kept->second.Sha256;
        }
        else
        {
            const auto digest = MCSha256::OfFile(path);

            if (!digest)
            {
                return std::unexpected(digest.error());
            }

            found.Sha256 = *digest;
            ++plan.HashedFiles;
        }

        plan.Hashes.insert_or_assign(onDisk, found);
        return found;
    }

    /// <summary>Whether the file found, <paramref name="hash"/>, is the one an update placed (so it can go).</summary>
    bool PlacedAsIs(const MCInstalled::MCFile* had, const MCSha256::MCDigest& hash)
    {
        return had != nullptr && had->Placed && had->Sha256 == hash;
    }

    /// <summary>Whether the file found, replaced or removed, is kept in Redux backup: when an update didn't place it.</summary>
    MCContentSwap::MCKeep KeepUnlessPlaced(const MCInstalled::MCFile* had, const MCSha256::MCDigest& hash)
    {
        return PlacedAsIs(had, hash) ? MCContentSwap::MCKeep::IfChanged : MCContentSwap::MCKeep::Always;
    }

    const MCInstalled::MCFile* Recorded(const MCInstalled::MCRecord& installed, std::string_view folded)
    {
        const auto found = installed.Files.find(folded);
        return found == installed.Files.end() ? nullptr : &found->second;
    }

    /// <summary>
    /// Adds to <paramref name="plan"/> the removal of <paramref name="had"/>, a recorded file, when it's on disk: kept in
    /// Redux backup when an update didn't place it as found, but deleted whatever it holds when its set is taken out
    /// (<paramref name="setTakenOut"/>: GET downloads it again).
    /// </summary>
    std::expected<void, std::string> AddRemoval(MCContentFolder& folder, const MCInstalled::MCFile& had,
                                                bool setTakenOut, const MCHashes& hashes, MCPlan& plan)
    {
        const auto found = folder.Find(had.Path);

        if (!found)
        {
            return std::unexpected(found.error());
        }

        if (found->Kind != MCContentFolder::MCKind::File)
        {
            return {};
        }

        const auto hashed = Hashed(folder, found->Path, &had, hashes, plan);

        if (!hashed)
        {
            return std::unexpected(hashed.error());
        }

        plan.Steps.push_back(
            MCStep{.Action = MCAction::Remove,
                   .Path = had.Path,
                   .OnDisk = found->Path,
                   .File = nullptr,
                   .Patch = nullptr,
                   .Found = hashed->Sha256,
                   .FoundSize = hashed->Size,
                   .FoundWritten = hashed->Written,
                   .Keep = setTakenOut ? MCContentSwap::MCKeep::Never : KeepUnlessPlaced(&had, hashed->Sha256)});
        return {};
    }

    /// <summary>Why a file can't be planned when something not a file is in its place.</summary>
    std::string InTheWay(std::string_view listed, const MCContentFolder::MCFound& found)
    {
        return found.Kind == MCContentFolder::MCKind::Link
                   ? std::format("{}: {} is a link, which updates don't follow", listed, found.Path)
                   : std::format("{}: {} is in its way", listed, found.Path);
    }
}

bool MCContentPlan::LeftToProgram(std::string_view platform, std::string_view path)
{
    return platform == MCReleaseFile::WindowsPlatform &&
           std::ranges::find(WindowsProgramFiles, FoldedPath(path)) != WindowsProgramFiles.end();
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
                                                       const std::set<std::string, std::less<>>& sets,
                                                       const MCHashes& hashes, std::string_view platform)
{
    MCPlan plan;
    plan.Record.Release = list.Release;
    plan.Record.List = listHash;
    plan.Record.Sets = sets;
    plan.LowestProgram = list.LowestProgram;
    std::set<std::string, std::less<>> zips;
    std::set<std::string, std::less<>> listed;

    for (const MCContentList::MCFile& file : list.Files)
    {
        if (!LeftToProgram(platform, file.Path))
        {
            plan.Record.SetBytes[file.Set] += file.Size;
        }
    }

    for (const MCContentList::MCFile& file : list.Files)
    {
        if (!sets.contains(file.Set) || LeftToProgram(platform, file.Path))
        {
            continue;
        }

        const std::string folded = FoldedPath(file.Path);
        listed.insert(folded);
        const MCInstalled::MCFile* const had = Recorded(installed, folded);
        const auto found = folder.Find(file.Path);

        if (!found)
        {
            return std::unexpected(found.error());
        }

        MCStep step;
        step.Path = file.Path;
        step.File = &file;
        MCInstalled::MCFile record{.Path = {}, .Set = file.Set, .Sha256 = file.Sha256, .Size = file.Size};

        switch (found->Kind)
        {
            case MCContentFolder::MCKind::Link:
            case MCContentFolder::MCKind::Blocked:
                return std::unexpected(InTheWay(file.Path, *found));
            case MCContentFolder::MCKind::Missing:
            {
                auto placed = folder.Place(file.Path);

                if (!placed)
                {
                    return std::unexpected(placed.error());
                }

                step.Action = MCAction::Fetch;
                step.OnDisk = std::move(*placed);
                break;
            }
            case MCContentFolder::MCKind::File:
            {
                step.OnDisk = found->Path;
                const auto hashed = Hashed(folder, found->Path, had, hashes, plan);

                if (!hashed)
                {
                    return std::unexpected(hashed.error());
                }

                step.Found = hashed->Sha256;
                step.FoundSize = hashed->Size;
                step.FoundWritten = hashed->Written;

                if (hashed->Sha256 == file.Sha256)
                {
                    step.Action = MCAction::Keep;
                    record.Written = hashed->Written;
                    record.Placed = PlacedAsIs(had, hashed->Sha256);
                }
                else
                {
                    const auto patch = std::ranges::find(file.Patches, hashed->Sha256, &MCContentList::MCPatch::From);
                    step.Action = patch != file.Patches.end() ? MCAction::Patch : MCAction::Fetch;
                    step.Patch = patch != file.Patches.end() ? &*patch : nullptr;
                    step.Keep = KeepUnlessPlaced(had, hashed->Sha256);
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
        if (listed.contains(folded) || LeftToProgram(platform, had.Path))
        {
            continue;
        }

        if (const auto removed = AddRemoval(folder, had, !sets.contains(had.Set), hashes, plan); !removed)
        {
            return std::unexpected(removed.error());
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

std::expected<MCPlan, std::string> MCContentPlan::Removal(MCContentFolder& folder,
                                                          const MCInstalled::MCRecord& installed,
                                                          const std::set<std::string, std::less<>>& sets,
                                                          const MCHashes& hashes, std::string_view platform)
{
    MCPlan plan;
    plan.Record = installed;
    plan.Record.Sets = sets;
    plan.Record.Files.clear();

    for (const auto& [folded, had] : installed.Files)
    {
        if (LeftToProgram(platform, had.Path))
        {
            continue;
        }

        if (sets.contains(had.Set))
        {
            plan.Record.Files.insert_or_assign(folded, had);
        }
        else if (const auto removed = AddRemoval(folder, had, true, hashes, plan); !removed)
        {
            return std::unexpected(removed.error());
        }
    }

    return plan;
}

std::string MCContentPlan::HashesText(const MCHashes& hashes)
{
    MCUpdateJson::MCJson files = MCUpdateJson::MCJson::array();

    for (const auto& [path, hash] : hashes)
    {
        files.push_back({{PathKey, path},
                         {SizeKey, hash.Size},
                         {WrittenKey, hash.Written},
                         {Sha256Key, MCSha256::Hex(hash.Sha256)}});
    }

    return MCUpdateJson::MCJson{{FormatKey, HashesFormat}, {FilesKey, std::move(files)}}.dump(1);
}

std::expected<MCHashes, std::string> MCContentPlan::ReadHashes(std::string_view text)
{
    return MCUpdateJson::Read(HashesFile,
                              [&]()
                              {
                                  const MCUpdateJson::MCJson json = MCUpdateJson::Object(text);

                                  if (MCUpdateJson::Count(json, FormatKey) != HashesFormat)
                                  {
                                      MCUpdateJson::Fail("\"{}\" isn't {}", FormatKey, HashesFormat);
                                  }

                                  MCHashes hashes;

                                  for (const MCUpdateJson::MCJson& file : MCUpdateJson::Array(json, FilesKey))
                                  {
                                      hashes.insert_or_assign(MCUpdateJson::Text(file, PathKey),
                                                              MCHash{.Size = MCUpdateJson::Count(file, SizeKey),
                                                                     .Written = MCUpdateJson::Integer(file, WrittenKey),
                                                                     .Sha256 = MCUpdateJson::Digest(file, Sha256Key)});
                                  }

                                  return hashes;
                              });
}
