#include "stdafx.h"
#include "update/MCInstalled.h"
#include "platform/MCFileSystem.h"
#include "update/MCInstalledJson.h"
#include "update/MCReleaseFile.h"
#include "update/MCContentList.h"
#include "update/MCContentUpdater.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"

namespace
{
    using namespace MCUpdateJson;

    constexpr std::string_view FormatKey = "format";
    constexpr std::string_view ReleaseKey = "release";
    constexpr std::string_view ListKey = "list";
    constexpr std::string_view SetsKey = "sets";
    constexpr std::string_view SetBytesKey = "setBytes";
    constexpr std::string_view FilesKey = "files";
    constexpr std::string_view PathKey = "path";
    constexpr std::string_view SetKey = "set";
    constexpr std::string_view Sha256Key = "sha256";
    constexpr std::string_view SizeKey = "size";
    constexpr std::string_view WrittenKey = "written";
    constexpr std::string_view PlacedKey = "placed";
    constexpr std::string_view SwapKey = "swap";
    constexpr std::string_view EpochKey = "epoch";
    constexpr uint64_t RecordFormat = 1;
    /// <summary>The due note's keys (its release is ReleaseKey's) and format.</summary>
    constexpr std::string_view LowestProgramKey = "lowestProgram";
    constexpr uint64_t DueFormat = 1;

    /// <summary>The record <paramref name="json"/> holds (an object); it throws MCBadFile at a rule broken.</summary>
    MCInstalled::MCRecord ReadRecord(const MCJson& json)
    {
        MCInstalled::MCRecord record;
        record.Release = Count(json, ReleaseKey);
        record.List = Digest(json, ListKey);
        record.Swap = TextOr(json, SwapKey, "");

        // Recorded since a key list could raise its epoch: an earlier record has none.
        record.Epoch = CountOr(json, EpochKey, 0);

        for (std::string& set : MCUpdateJson::Strings(json, SetsKey))
        {
            record.Sets.insert(std::move(set));
        }

        // Recorded since the HD pack could be got later (HD OPTIONS): an earlier record has none.
        if (json.contains(SetBytesKey))
        {
            const MCJson& setBytes = Child(json, SetBytesKey);

            for (const auto& [set, bytes] : setBytes.items())
            {
                record.SetBytes.insert_or_assign(set, Count(setBytes, set));
            }
        }

        for (const MCJson& entry : Array(json, FilesKey))
        {
            MCInstalled::MCFile file;
            file.Path = MCUpdateJson::Text(entry, PathKey);

            if (const std::string problem = MCContentList::PathProblem(file.Path); !problem.empty())
            {
                Fail("\"{}\" {}", file.Path, problem);
            }

            file.Set = MCUpdateJson::Text(entry, SetKey);
            file.Sha256 = Digest(entry, Sha256Key);
            file.Size = Count(entry, SizeKey);
            file.Written = MCUpdateJson::Integer(entry, WrittenKey);
            const MCJson& placed = Member(entry, PlacedKey);

            if (!placed.is_boolean())
            {
                Fail("\"{}\" isn't true or false", PlacedKey);
            }

            file.Placed = placed.get<bool>();
            record.Files.insert_or_assign(MCContentList::FoldedPath(file.Path), std::move(file));
        }

        return record;
    }
}

std::optional<int64_t> MCInstalled::WrittenAt(const std::filesystem::path& path)
{
    // As the update's disk tells it, so a plan's times and the swap's are the same.
    const MCUpdateDisk::MCStat stat = MakeDisk()->Stat(path);

    if (!stat.Exists || !stat.Error.empty())
    {
        return std::nullopt;
    }

    return stat.Written;
}

MCUpdateJson::MCJson MCInstalled::ToJson(const MCRecord& record)
{
    MCJson files = MCJson::array();

    for (const auto& [folded, file] : record.Files)
    {
        files.push_back({{PathKey, file.Path},
                         {SetKey, file.Set},
                         {Sha256Key, MCSha256::Hex(file.Sha256)},
                         {SizeKey, file.Size},
                         {WrittenKey, file.Written},
                         {PlacedKey, file.Placed}});
    }

    MCJson setBytes = MCJson::object();

    for (const auto& [set, bytes] : record.SetBytes)
    {
        setBytes[set] = bytes;
    }

    return MCJson{{FormatKey, RecordFormat}, {ReleaseKey, record.Release}, {ListKey, MCSha256::Hex(record.List)},
                  {EpochKey, record.Epoch},  {SetsKey, record.Sets},       {SetBytesKey, std::move(setBytes)},
                  {SwapKey, record.Swap},    {FilesKey, std::move(files)}};
}

std::string MCInstalled::Text(const MCRecord& record)
{
    return ToJson(record).dump(1);
}

std::expected<MCInstalled::MCRecord, std::string> MCInstalled::FromText(std::string_view text)
{
    return MCUpdateJson::Read(RecordPath, [&]() { return ReadRecord(Object(text)); });
}

std::expected<MCInstalled::MCRecord, std::string> MCInstalled::FromJson(const MCUpdateJson::MCJson& json)
{
    return MCUpdateJson::Read(RecordPath,
                              [&]()
                              {
                                  if (!json.is_object())
                                  {
                                      Fail("isn't a JSON object");
                                  }

                                  return ReadRecord(json);
                              });
}

std::expected<MCInstalled::MCRecord, std::string> MCInstalled::Read(MCUpdateDisk& disk,
                                                                    const std::filesystem::path& gameFolder)
{
    const auto record = ReadFile(disk, gameFolder / MCFileSystem::FromUtf8(RecordPath), FromText);

    if (!record)
    {
        return std::unexpected(record.error());
    }

    return record->value_or(MCRecord{});
}

std::expected<MCInstalled::MCRecord, std::string> MCInstalled::Read(const std::filesystem::path& gameFolder)
{
    return Read(*MakeDisk(), gameFolder);
}

bool MCInstalled::Has(const MCRecord& record, const std::filesystem::path& gameFolder, std::string_view set)
{
    return record.Release != 0 ? record.Sets.contains(set) : MCContentUpdater::SetsFound(gameFolder).contains(set);
}

std::expected<void, std::string> MCInstalled::NoteDue(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                      const MCDue& due)
{
    const std::filesystem::path path = gameFolder / MCFileSystem::FromUtf8(DueFile);
    const std::string text = MCJson{
        {FormatKey, DueFormat},
        {EpochKey, due.Epoch},
        {ReleaseKey, due.Release},
        {LowestProgramKey,
         due.LowestProgram}}.dump();

    if (!WriteDurably(disk, path, text))
    {
        return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(path)));
    }

    return {};
}

std::optional<MCInstalled::MCDue> MCInstalled::ReadDue(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    const auto due =
        ReadFile(disk, gameFolder / MCFileSystem::FromUtf8(DueFile),
                 [](std::string_view text)
                 {
                     return MCUpdateJson::Read(DueFile,
                                               [&]
                                               {
                                                   const MCJson json = Object(text);
                                                   return MCDue{.Epoch = CountOr(json, EpochKey, 0),
                                                                .Release = Count(json, ReleaseKey),
                                                                .LowestProgram = Count(json, LowestProgramKey)};
                                               });
                 });

    if (!due)
    {
        SDL_Log("Updates: the content due can't be read (%s): the start doesn't wait for it", due.error().c_str());
        return std::nullopt;
    }

    return *due;
}

MCInstalled::MCStartNeed MCInstalled::AtStart(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                              const MCRecord& record, uint64_t runningBuild, uint64_t lowestContent)
{
    if ((record.Release == 0 && disk.Stat(gameFolder / MCFileSystem::FromUtf8(MCContentUpdater::ChosenFile)).Exists) ||
        record.Release < lowestContent)
    {
        return MCStartNeed::Needed;
    }

    // Due only to a program that takes it: an older one plays the files it has, and is offered the update again.
    if (const std::optional<MCDue> due = ReadDue(disk, gameFolder);
        due &&
        MCReleaseFile::MCOrder{due->Epoch, due->Release} > MCReleaseFile::MCOrder{record.Epoch, record.Release} &&
        runningBuild >= due->LowestProgram)
    {
        return MCStartNeed::Due;
    }

    return MCStartNeed::Nothing;
}
