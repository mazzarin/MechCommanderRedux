#include "stdafx.h"
#include "network/MCGameDataHash.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCLogisticsShared.h"
#include "platform/MCFileSystem.h"
#include <limits>
// xxHash, header-only: its functions inline in this file alone.
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace
{
    /// <summary>The kind of the editors' backups, which the game never reads.</summary>
    constexpr std::array<std::string_view, 1> BackupKinds = {".BAK"};
    /// <summary>The kinds of a map's pictures (the tactical map's and the briefing's), which only show it.</summary>
    constexpr std::array<std::string_view, 2> PictureKinds = {".TGA", ".GIF"};
    /// <summary>
    /// The mechs' shape data their types read (MCBattleMechType): the hot spots their weapons fire from and how many
    /// a gesture has, their jumps' and falls' heights, their jump jets.
    /// </summary>
    constexpr std::array<std::string_view, 4> ShapeDataKinds = {".HSP", ".INF", ".OUT", ".JMP"};
    /// <summary>The ABL libraries' kind (the scenarios' ABLibraries).</summary>
    constexpr std::array<std::string_view, 1> LibraryKinds = {".ABX"};
    /// <summary>The kinds of ABL's sources: a script, a library, an include; what each includes counts with it.</summary>
    constexpr std::array<std::string_view, 3> ScriptKinds = {".ABL", ".ABX", ".ABI"};
    /// <summary>The game system (MCScenario::LoadGameSystem), under MissionPath.</summary>
    constexpr std::string_view GameSystemFile = "gamesys";
    /// <summary>The extension of the scenarios and purchase lists, under MissionPath.</summary>
    constexpr std::string_view FitExtension = ".fit";
    /// <summary>The extension of a scenario's script, under MissionPath.</summary>
    constexpr std::string_view ScriptExtension = ".abl";
    /// <summary>The length hashed for a file the game hasn't (no file is that long).</summary>
    constexpr uint64_t MissingLength = std::numeric_limits<uint64_t>::max();
    /// <summary>What a report says of a file the game hasn't (the original's report too).</summary>
    constexpr uint32_t NoFile = 0;
    /// <summary>What a report says in place of a hash that folds to <see cref="NoFile"/>.</summary>
    constexpr uint32_t ZeroFolded = 1;
    /// <summary>How far a 64-bit hash's top half is shifted onto its bottom half to fold it to 32 bits.</summary>
    constexpr uint32_t FoldShift = std::numeric_limits<uint32_t>::digits;

    /// <summary>Appends <paramref name="word"/> to <paramref name="bytes"/>, little-endian (the same on every machine).</summary>
    void PutWord(std::vector<uint8_t>& bytes, uint64_t word)
    {
        for (size_t i = 0; i < sizeof(word); i++)
        {
            bytes.push_back(static_cast<uint8_t>(word >> (CHAR_BIT * i)));
        }
    }

    /// <summary>
    /// A game path as names compare: <c>\</c> separators, a-z upper-cased (the game ignores their case; as the archives'
    /// index, not the locale's toupper, so every machine makes the same key).
    /// </summary>
    std::string Key(std::string_view gamePath)
    {
        std::string key(gamePath);

        for (char& c : key)
        {
            c = c == '/' ? '\\' : c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        }

        return key;
    }

    /// <summary>Whether <paramref name="key"/> (upper case) ends in one of <paramref name="kinds"/>.</summary>
    bool OfKind(std::string_view key, std::span<const std::string_view> kinds)
    {
        return std::ranges::any_of(kinds, [key](std::string_view kind) { return key.ends_with(kind); });
    }

    /// <summary>
    /// The files in game folder <paramref name="folder"/> (ending in <c>\</c>) whose names (upper case, no folder)
    /// <paramref name="wanted"/> takes, as the game finds them: the loose ones (the user folder's and the install's)
    /// and every open archive's. By key, each once.
    /// </summary>
    std::vector<std::string> FolderFiles(std::string_view folder, const std::function<bool(std::string_view)>& wanted)
    {
        const std::string folderKey = Key(folder);
        std::map<std::string, std::string> files;

        const auto add = [&](std::string_view name)
        {
            if (const std::string nameKey = Key(name); wanted(nameKey))
            {
                files.try_emplace(folderKey + nameKey, GamePath(folder, name, ""));
            }
        };

        for (const std::string& name : MCFileSystem::FindFiles(GamePath(folder, "*", "")))
        {
            add(name);
        }

        for (const std::unique_ptr<MCFastFile>& fastFile : MCGameContext::Current().FastFiles().Files())
        {
            for (const MCFileEntry& entry : fastFile->GetEntries())
            {
                if (const std::string key = Key(entry.GetName()); key.size() > folderKey.size() &&
                                                                  key.starts_with(folderKey) &&
                                                                  key.find('\\', folderKey.size()) == std::string::npos)
                {
                    add(entry.GetName().substr(folderKey.size()));
                }
            }
        }

        std::vector<std::string> paths;
        paths.reserve(files.size());

        for (auto& [key, path] : files)
        {
            paths.push_back(std::move(path));
        }

        return paths;
    }

    /// <summary>The files in game folder <paramref name="folder"/> of <paramref name="kinds"/>.</summary>
    std::vector<std::string> FolderFiles(std::string_view folder, std::span<const std::string_view> kinds)
    {
        return FolderFiles(folder, [kinds](std::string_view name) { return OfKind(name, kinds); });
    }

    /// <summary>The folder of <paramref name="gamePath"/> with its <c>\</c> (none: empty), as ABL takes a module's.</summary>
    std::string_view Folder(std::string_view gamePath)
    {
        const size_t slash = gamePath.rfind('\\');
        return slash == std::string_view::npos ? std::string_view() : gamePath.substr(0, slash + 1);
    }

    /// <summary>
    /// The files ABL source <paramref name="source"/> includes, as MCAblScanner opens them: <c>#include "file"</c> as
    /// written, <c>#include_ "file"</c> in <paramref name="moduleFolder"/> (the folder of the module's own file, the one
    /// compiled, whatever file includes it). Comments are skipped.
    /// </summary>
    std::vector<std::string> Includes(std::span<const uint8_t> source, std::string_view moduleFolder)
    {
        constexpr std::string_view LineComment = "//";
        constexpr std::string_view BlockCommentStart = "/*";
        constexpr std::string_view BlockCommentEnd = "*/";
        constexpr std::string_view Spaces = " \t";
        constexpr std::string_view WordEnds = " \t\r\n";
        const std::string_view text(reinterpret_cast<const char*>(source.data()), source.size());
        std::vector<std::string> files;
        size_t at = 0;

        while (at < text.size())
        {
            const std::string_view rest = text.substr(at);

            if (rest.starts_with(LineComment))
            {
                at = text.find('\n', at);
                continue;
            }

            if (rest.starts_with(BlockCommentStart))
            {
                const size_t end = text.find(BlockCommentEnd, at + BlockCommentStart.size());
                at = end == std::string_view::npos ? end : end + BlockCommentEnd.size();
                continue;
            }

            if (rest.front() != '#')
            {
                at++;
                continue;
            }

            const size_t wordEnd = std::min(text.find_first_of(WordEnds, at + 1), text.size());
            const std::string_view directive = text.substr(at + 1, wordEnd - at - 1);
            at = wordEnd;

            if (directive != "include" && directive != "include_")
            {
                continue;
            }

            const size_t open = text.find_first_not_of(Spaces, at);

            if (open == std::string_view::npos || text[open] != '"')
            {
                continue;
            }

            const size_t close = text.find('"', open + 1);

            if (close == std::string_view::npos)
            {
                break;
            }

            const std::string_view name = text.substr(open + 1, close - open - 1);
            files.push_back(directive == "include_" ? GamePath(moduleFolder, name, "") : std::string(name));
            at = close + 1;
        }

        return files;
    }

    /// <summary>A file's length and XXH3-64 of its contents.</summary>
    struct MCFileDigest
    {
        uint64_t Length = 0;
        uint64_t Hash = 0;
    };

    /// <summary>A part's files, each counted once, by key: its game path and its digest (none: the game hasn't it).</summary>
    class MCDataFiles
    {
    public:
        /// <summary>
        /// Adds <paramref name="gamePath"/> (not a backup) as the game finds it now, and for a script what it includes,
        /// its own folder the module's.
        /// </summary>
        void Add(std::string_view gamePath) { Add(gamePath, Folder(gamePath)); }

        /// <summary>The number of files.</summary>
        [[nodiscard]] size_t Count() const { return _Files.size(); }

        /// <summary>The files' game paths, by key.</summary>
        [[nodiscard]] std::vector<std::string> Paths() const
        {
            std::vector<std::string> paths;
            paths.reserve(_Files.size());

            for (const auto& [key, file] : _Files)
            {
                paths.push_back(file.Path);
            }

            return paths;
        }

        /// <summary>The files' lengths together (for the log).</summary>
        [[nodiscard]] uint64_t Bytes() const
        {
            uint64_t bytes = 0;

            for (const auto& [key, file] : _Files)
            {
                bytes += file.Digest.has_value() ? file.Digest->Length : 0;
            }

            return bytes;
        }

        /// <summary>
        /// Appends the files to <paramref name="hashed"/>, in key order: each key, a 0, then the length and hash of
        /// what the game finds (little-endian; <see cref="MissingLength"/> and 0 for a file it hasn't).
        /// </summary>
        void AppendTo(std::vector<uint8_t>& hashed) const
        {
            for (const auto& [key, file] : _Files)
            {
                hashed.insert(hashed.end(), key.begin(), key.end());
                hashed.push_back(0);
                PutWord(hashed, file.Digest.has_value() ? file.Digest->Length : MissingLength);
                PutWord(hashed, file.Digest.has_value() ? file.Digest->Hash : 0);
            }
        }

    private:
        /// <summary>A file of the part.</summary>
        struct MCDataFile
        {
            std::string Path;
            std::optional<MCFileDigest> Digest;
        };

        /// <summary>Adds <paramref name="gamePath"/>, a source of the module whose file is in <paramref name="moduleFolder"/>.</summary>
        void Add(std::string_view gamePath, std::string_view moduleFolder)
        {
            const std::string key = Key(gamePath);

            if (OfKind(key, BackupKinds))
            {
                return;
            }

            // A source is read for its includes once for each module folder it is a part of (#include_ follows it).
            if (OfKind(key, ScriptKinds) && !_Scanned.insert(std::format("{}|{}", key, Key(moduleFolder))).second)
            {
                return;
            }

            auto [file, added] = _Files.try_emplace(key, MCDataFile{.Path = std::string(gamePath)});

            if (!added && !OfKind(key, ScriptKinds))
            {
                return;
            }

            MCFile reader;

            if (reader.Open(gamePath) != 0)
            {
                return;
            }

            std::vector<uint8_t> contents(reader.GetLength());
            contents.resize(static_cast<size_t>(std::max(reader.Read(contents), 0)));
            file->second.Digest =
                MCFileDigest{.Length = contents.size(), .Hash = XXH3_64bits(contents.data(), contents.size())};

            if (OfKind(key, ScriptKinds))
            {
                for (const std::string& included : Includes(contents, moduleFolder))
                {
                    Add(included, moduleFolder);
                }
            }
        }

        /// <summary>The files by key (sorted, so every game hashes them in the same order).</summary>
        std::map<std::string, MCDataFile> _Files;
        /// <summary>The sources read for their includes: key and module folder.</summary>
        std::set<std::string> _Scanned;
    };

    /// <summary>Every match's gameplay files (the rule is in network/MCGameDataHash.h).</summary>
    MCDataFiles SharedFiles()
    {
        MCDataFiles files;
        const auto all = [](std::string_view) { return true; };

        for (const std::string& folder : {std::cref(ObjectPath), std::cref(WarriorPath), std::cref(ProfilePath)})
        {
            for (const std::string& path : FolderFiles(folder, all))
            {
                files.Add(path);
            }
        }

        files.Add(GamePath(MissionPath, GameSystemFile, FitExtension));

        for (const std::string& path : FolderFiles(MissionPath, LibraryKinds))
        {
            files.Add(path);
        }

        for (const std::string& path : FolderFiles(ShapesPath, ShapeDataKinds))
        {
            files.Add(path);
        }

        return files;
    }

    /// <summary>
    /// The gameplay files of the map <paramref name="map"/> (a scenario's TerrainFileName): every file in TerrainPath
    /// named after it (the map's own name before the first dot), but its pictures.
    /// </summary>
    std::vector<std::string> MapFiles(std::string_view map)
    {
        const std::string prefix = Key(map) + '.';
        return FolderFiles(TerrainPath, [&prefix](std::string_view name)
                           { return name.starts_with(prefix) && !OfKind(name, PictureKinds); });
    }

    /// <summary>
    /// Mission file <paramref name="missionFile"/>'s gameplay files, by what it names as the game reads it
    /// (MCLogistics::LoadCampaign, MCScenario::Load): itself, its purchase list, its scenario, the scenario's script and
    /// map. A file that can't be read names nothing more.
    /// </summary>
    MCDataFiles MissionFiles(std::string_view missionFile)
    {
        MCDataFiles files;
        files.Add(missionFile);
        MCPacketFile packets;
        MCFitIniFile mission;

        if (packets.Open(missionFile) != 0 || packets.SeekPacket(0) != 0 ||
            mission.Open(packets, static_cast<uint32_t>(packets.GetPacketSize())) != 0)
        {
            return files;
        }

        if (mission.SeekBlock("General") == 0)
        {
            files.Add(GamePath(MissionPath, MPPurchaseFileName(mission), FitExtension));
        }

        if (mission.SeekBlock("Multiplayer") != 0)
        {
            return files;
        }

        const MCFitResult<std::string> scenarioName = mission.Read<std::string>("MissionName");

        if (!scenarioName.has_value())
        {
            return files;
        }

        const std::string scenarioPath = GamePath(MissionPath, *scenarioName, FitExtension);
        files.Add(scenarioPath);
        MCFitIniFile scenario;

        if (scenario.Open(scenarioPath) != 0)
        {
            return files;
        }

        if (scenario.SeekBlock("Script") == 0)
        {
            if (const MCFitResult<std::string> script = scenario.Read<std::string>("ScenarioScript"))
            {
                files.Add(GamePath(MissionPath, *script, ScriptExtension));
            }
        }

        if (scenario.SeekBlock("TerrainSystem") == 0)
        {
            if (const MCFitResult<std::string> map = scenario.Read<std::string>("TerrainFileName"))
            {
                for (const std::string& path : MapFiles(*map))
                {
                    files.Add(path);
                }
            }
        }

        return files;
    }

    /// <summary>XXH3-64 of <paramref name="files"/> as <see cref="MCDataFiles::AppendTo"/> lays them out.</summary>
    uint64_t HashOf(const MCDataFiles& files)
    {
        std::vector<uint8_t> hashed;
        files.AppendTo(hashed);
        return XXH3_64bits(hashed.data(), hashed.size());
    }
}

std::vector<std::string> MCSharedDataFiles()
{
    return SharedFiles().Paths();
}

std::vector<std::string> MCMissionDataFiles(std::string_view missionFile)
{
    return MissionFiles(missionFile).Paths();
}

uint64_t MCSharedDataHash()
{
    std::optional<uint64_t>& kept = MCGameContext::Current().SharedDataHash();

    if (!kept.has_value())
    {
        const auto start = std::chrono::steady_clock::now();
        const MCDataFiles files = SharedFiles();
        kept = HashOf(files);
        const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - start;
        SDL_Log("Game data: %zu shared gameplay files (%llu bytes) hashed in %.1f ms", files.Count(),
                static_cast<unsigned long long>(files.Bytes()), took.count());
    }

    return *kept;
}

uint32_t MCMissionDataHash(std::string_view missionFile)
{
    if (MCFile file; file.Open(missionFile) != 0)
    {
        return NoFile;
    }

    // The shared part's hash, then the mission's files.
    std::vector<uint8_t> hashed;
    PutWord(hashed, MCSharedDataHash());
    MissionFiles(missionFile).AppendTo(hashed);
    const uint64_t hash = XXH3_64bits(hashed.data(), hashed.size());
    const auto folded = static_cast<uint32_t>(hash ^ (hash >> FoldShift));
    return folded != NoFile ? folded : ZeroFolded;
}
