#include "stdafx.h"
#include "update/MCContentList.h"
#include "update/MCUpdateJson.h"

namespace
{
    using namespace MCUpdateJson;
    using namespace MCContentList;

    constexpr std::string_view FormatKey = "format";
    constexpr std::string_view ReleaseKey = "release";
    constexpr std::string_view LowestProgramKey = "lowestProgram";
    constexpr std::string_view ZipsKey = "zips";
    constexpr std::string_view FilesKey = "files";
    constexpr std::string_view NameKey = "name";
    constexpr std::string_view SizeKey = "size";
    constexpr std::string_view Sha256Key = "sha256";
    constexpr std::string_view PathKey = "path";
    constexpr std::string_view SetKey = "set";
    constexpr std::string_view ZipKey = "zip";
    constexpr std::string_view EarlierKey = "earlier";
    constexpr std::string_view PatchesKey = "patches";
    constexpr std::string_view FromKey = "from";
    constexpr char Separator = '/';
    /// <summary>Characters Windows refuses in a name.</summary>
    constexpr std::string_view WindowsRefuses = "\\:*?\"<>|";
    /// <summary>The first character that isn't a control character, and DEL.</summary>
    constexpr unsigned char FirstPrintable = 0x20;
    constexpr unsigned char Delete = 0x7f;
    /// <summary>Windows' device names, which a file can't take whatever its extension.</summary>
    constexpr std::array<std::string_view, 4> ReservedNames = {"CON", "PRN", "AUX", "NUL"};
    constexpr std::array<std::string_view, 2> NumberedReservedNames = {"COM", "LPT"};
    constexpr size_t NumberedReservedLength = 4;
    /// <summary>The longest path a list may name, in bytes (the game folder's own path comes on top).</summary>
    constexpr size_t MaxPathBytes = 200;

    /// <summary>Whether the part <paramref name="part"/> is one of Windows' device names, with or without an extension.</summary>
    bool IsReservedName(std::string_view part)
    {
        const std::string stem = FoldedPath(part.substr(0, part.find('.')));

        if (std::ranges::find(ReservedNames, stem) != ReservedNames.end())
        {
            return true;
        }

        return stem.size() == NumberedReservedLength &&
               std::ranges::find(NumberedReservedNames, std::string_view(stem).substr(0, 3)) !=
                   NumberedReservedNames.end() &&
               stem[3] >= '0' && stem[3] <= '9';
    }

    /// <summary>A host path (a zip's or patch's name): the same rules as a content path, never under the update folders.</summary>
    std::string HostName(const MCJson& json, std::string_view key)
    {
        std::string name = Text(json, key);

        if (const std::string problem = PathProblem(name); !problem.empty())
        {
            Fail("\"{}\" {}", name, problem);
        }

        return name;
    }

    MCPatch ReadPatch(const MCJson& json)
    {
        MCPatch patch;
        patch.From = Digest(json, FromKey);
        patch.Name = HostName(json, NameKey);
        patch.Size = Count(json, SizeKey);
        patch.Sha256 = Digest(json, Sha256Key);
        return patch;
    }

    MCFile ReadFile(const MCJson& json, const std::set<std::string, std::less<>>& zips)
    {
        MCFile file;
        file.Path = Text(json, PathKey);

        if (const std::string problem = PathProblem(file.Path); !problem.empty())
        {
            Fail("\"{}\" {}", file.Path, problem);
        }

        file.Set = Text(json, SetKey);
        file.Size = Count(json, SizeKey);
        file.Sha256 = Digest(json, Sha256Key);
        file.Zip = Text(json, ZipKey);

        if (!zips.contains(file.Zip))
        {
            Fail("\"{}\" is in the zip \"{}\", which isn't listed", file.Path, file.Zip);
        }

        for (const MCJson& earlier : Array(json, EarlierKey, false))
        {
            file.Earlier.push_back(DigestOf(earlier, EarlierKey));
        }

        for (const MCJson& patch : Array(json, PatchesKey, false))
        {
            file.Patches.push_back(ReadPatch(patch));

            if (file.Patches.back().From == file.Sha256)
            {
                Fail("\"{}\" has a patch from itself", file.Path);
            }
        }

        return file;
    }

    /// <summary>Refuses two files alike but for case, and a file where another's folder is.</summary>
    void CheckNoClashes(const std::vector<MCFile>& files)
    {
        std::set<std::string, std::less<>> paths;

        for (const MCFile& file : files)
        {
            if (!paths.insert(FoldedPath(file.Path)).second)
            {
                Fail("\"{}\" is listed twice (case aside)", file.Path);
            }
        }

        for (const MCFile& file : files)
        {
            const std::string folded = FoldedPath(file.Path);

            for (size_t slash = folded.find(Separator); slash != std::string::npos;
                 slash = folded.find(Separator, slash + 1))
            {
                if (paths.contains(std::string_view(folded).substr(0, slash)))
                {
                    Fail("\"{}\" is in a folder another file is named as", file.Path);
                }
            }
        }
    }
}

std::string MCContentList::FoldedPath(std::string_view path)
{
    std::string folded(path);

    for (char& c : folded)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }

    return folded;
}

std::string MCContentList::PathProblem(std::string_view path)
{
    if (path.empty())
    {
        return "is empty";
    }

    if (path.size() > MaxPathBytes)
    {
        return std::format("is longer than {} bytes", MaxPathBytes);
    }

    if (path.front() == Separator)
    {
        return "starts at the disk's root";
    }

    for (const char c : path)
    {
        const auto byte = static_cast<unsigned char>(c);

        if (byte < FirstPrintable || byte == Delete)
        {
            return "holds a control character";
        }

        if (WindowsRefuses.find(c) != std::string_view::npos)
        {
            return std::format("holds '{}', which Windows refuses in a name", c);
        }
    }

    size_t start = 0;

    while (start <= path.size())
    {
        const size_t end = std::min(path.find(Separator, start), path.size());
        const std::string_view part = path.substr(start, end - start);

        if (part.empty())
        {
            return "has an empty part";
        }

        if (part == "." || part == "..")
        {
            return std::format("has a \"{}\" part", part);
        }

        if (part.back() == '.' || part.back() == ' ')
        {
            return "has a part ending in a dot or space, which Windows drops";
        }

        if (IsReservedName(part))
        {
            return std::format("has \"{}\", a name Windows keeps for a device", part);
        }

        if (start == 0 &&
            (FoldedPath(part) == FoldedPath(UpdateFolder) || FoldedPath(part) == FoldedPath(BackupFolder)))
        {
            return std::format("is \"{}\" or in it, which updates keep for themselves", part);
        }

        start = end + 1;
    }

    return {};
}

std::expected<MCList, std::string> MCContentList::Read(std::string_view payload)
{
    return MCUpdateJson::Read("the content list",
                              [&]()
                              {
                                  const MCJson json = Object(payload);
                                  MCList list;
                                  list.Format = Count(json, FormatKey);
                                  list.Release = Count(json, ReleaseKey);
                                  list.LowestProgram = Count(json, LowestProgramKey);
                                  std::set<std::string, std::less<>> zipNames;

                                  for (const MCJson& entry : Array(json, ZipsKey))
                                  {
                                      MCZip zip{HostName(entry, NameKey), Count(entry, SizeKey),
                                                Digest(entry, Sha256Key)};

                                      if (!zipNames.insert(zip.Name).second)
                                      {
                                          Fail("the zip \"{}\" is listed twice", zip.Name);
                                      }

                                      list.Zips.push_back(std::move(zip));
                                  }

                                  for (const MCJson& entry : Array(json, FilesKey))
                                  {
                                      list.Files.push_back(ReadFile(entry, zipNames));
                                  }

                                  CheckNoClashes(list.Files);
                                  return list;
                              });
}

std::string MCContentList::Write(const MCList& list)
{
    MCJson zips = MCJson::array();

    for (const MCZip& zip : list.Zips)
    {
        zips.push_back({{NameKey, zip.Name}, {SizeKey, zip.Size}, {Sha256Key, MCSha256::Hex(zip.Sha256)}});
    }

    MCJson files = MCJson::array();

    for (const MCFile& file : list.Files)
    {
        MCJson entry{{PathKey, file.Path},
                     {SetKey, file.Set},
                     {SizeKey, file.Size},
                     {Sha256Key, MCSha256::Hex(file.Sha256)},
                     {ZipKey, file.Zip}};

        if (!file.Earlier.empty())
        {
            MCJson earlier = MCJson::array();

            for (const MCSha256::MCDigest& digest : file.Earlier)
            {
                earlier.push_back(MCSha256::Hex(digest));
            }

            entry[std::string(EarlierKey)] = std::move(earlier);
        }

        if (!file.Patches.empty())
        {
            MCJson patches = MCJson::array();

            for (const MCPatch& patch : file.Patches)
            {
                patches.push_back({{FromKey, MCSha256::Hex(patch.From)},
                                   {NameKey, patch.Name},
                                   {SizeKey, patch.Size},
                                   {Sha256Key, MCSha256::Hex(patch.Sha256)}});
            }

            entry[std::string(PatchesKey)] = std::move(patches);
        }

        files.push_back(std::move(entry));
    }

    return MCJson{{FormatKey, list.Format},
                  {ReleaseKey, list.Release},
                  {LowestProgramKey, list.LowestProgram},
                  {ZipsKey, std::move(zips)},
                  {FilesKey, std::move(files)}}
        .dump(1);
}
