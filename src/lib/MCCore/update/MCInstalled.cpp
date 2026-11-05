#include "stdafx.h"
#include "update/MCInstalled.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"
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
    constexpr uint64_t RecordFormat = 1;
}

std::optional<int64_t> MCInstalled::WrittenAt(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);

    if (error)
    {
        return std::nullopt;
    }

    return static_cast<int64_t>(written.time_since_epoch().count());
}

std::string MCInstalled::Text(const MCRecord& record)
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

    return MCJson{
        {FormatKey, RecordFormat},   {ReleaseKey, record.Release},       {ListKey, MCSha256::Hex(record.List)},
        {SetsKey, record.Sets},      {SetBytesKey, std::move(setBytes)}, {SwapKey, record.Swap},
        {FilesKey, std::move(files)}}
        .dump(1);
}

std::expected<MCInstalled::MCRecord, std::string> MCInstalled::FromText(std::string_view text)
{
    return MCUpdateJson::Read(
        RecordPath,
        [&]()
        {
            const MCJson json = Object(text);
            MCRecord record;
            record.Release = Count(json, ReleaseKey);
            record.List = Digest(json, ListKey);
            record.Swap = TextOr(json, SwapKey, "");

            for (const MCJson& set : Array(json, SetsKey))
            {
                if (!set.is_string())
                {
                    Fail("a set isn't a string");
                }

                record.Sets.insert(set.get<std::string>());
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
                MCFile file;
                file.Path = MCUpdateJson::Text(entry, PathKey);

                if (const std::string problem = MCContentList::PathProblem(file.Path); !problem.empty())
                {
                    Fail("\"{}\" {}", file.Path, problem);
                }

                file.Set = MCUpdateJson::Text(entry, SetKey);
                file.Sha256 = Digest(entry, Sha256Key);
                file.Size = Count(entry, SizeKey);
                const MCJson& written = Member(entry, WrittenKey);

                if (!written.is_number_integer())
                {
                    Fail("\"{}\" isn't a whole number", WrittenKey);
                }

                file.Written = written.get<int64_t>();
                const MCJson& placed = Member(entry, PlacedKey);

                if (!placed.is_boolean())
                {
                    Fail("\"{}\" isn't true or false", PlacedKey);
                }

                file.Placed = placed.get<bool>();
                record.Files.insert_or_assign(MCContentList::FoldedPath(file.Path), std::move(file));
            }

            return record;
        });
}

std::expected<MCInstalled::MCRecord, std::string> MCInstalled::Read(const std::filesystem::path& gameFolder)
{
    const std::filesystem::path path = gameFolder / MCFileSystem::FromUtf8(RecordPath);
    std::error_code error;

    if (!std::filesystem::exists(path, error))
    {
        return MCRecord{};
    }

    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (!file && !file.eof())
    {
        return std::unexpected(std::format("{} can't be read", MCFileSystem::ToUtf8(path)));
    }

    return FromText(text);
}
