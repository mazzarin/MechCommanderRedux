#include "stdafx.h"
#include "../MCGameFolder.h"
#include "MCContentFixture.h"
#include "MCFastFileFixture.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "TestGame.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGameContext.h"
#include "main/MCSystemConfig.h"
#include "network/MCGameDataHash.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentUpdater.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

// A launcher's match (SPEC-install-and-updates.md's A launcher's game folder): a lobby app (MechCommander Online)
// starts the program of a Redux HD install with --data naming its own copy of the game and -host or -join. The game
// plays the install (MCGameFolder::Played), and the launcher's folder gives only its maps and mods
// (MCFileSystem::SetLauncherRoot): found there before the install, its archives in the set in name order as the
// original's MCX.EXE searches its own, and never written. The start's update in the install is ContentUpdaterTests'.

namespace
{
    using namespace MCContentFixture;
    using namespace MCFastFileFixture;

    constexpr std::string_view Group = "mc_launcher_folder_tests";

    /// <summary>The command line of a launcher's match, as MechCommander Online starts a guest's game.</summary>
    constexpr std::string_view LauncherJoin = "-player ANN -join 127.0.0.1:28800";
    constexpr std::string_view LauncherHost = "-player BOB -host 28800 -session \"MCO 1234\" -maxplayers 4";

    /// <summary>The mission a launcher's map gives, and the files it plays from its archive.</summary>
    constexpr std::string_view MapMission = "data\\savegame\\MCOMAP.MPK";
    constexpr std::string_view MapScenario = "data\\missions\\mcomap.fit";
    constexpr std::string_view MapScript = "data\\missions\\mcomap.abl";
    constexpr std::string_view MapTerrain = "data\\terrain\\mcomap.dat";
    constexpr std::string_view MapScenarioText = "FITini\n[TerrainSystem]\nst TerrainFileName = \"mcomap\"\n"
                                                 "[Script]\nst ScenarioScript = \"mcomap\"\nFITend\n";

    /// <summary>A gameplay file the original's MISSION.FST holds, and one of the objects' loose files.</summary>
    constexpr std::string_view GameSystem = "data\\missions\\gamesys.fit";
    constexpr std::string_view Profile = "data\\missions\\profiles\\PM100100.fit";
    constexpr std::string_view Objects = "data\\objects\\compbas.csv";

    /// <summary>The bytes of <paramref name="text"/>.</summary>
    std::vector<uint8_t> Bytes(std::string_view text)
    {
        return {text.begin(), text.end()};
    }

    /// <summary>An archive at <paramref name="path"/> of <paramref name="entries"/> (game path, text).</summary>
    void WriteArchive(const std::filesystem::path& path,
                      std::initializer_list<std::pair<std::string_view, std::string_view>> entries)
    {
        std::vector<FastEntry> built;

        for (const auto& [name, text] : entries)
        {
            built.push_back({.Name = std::string(name), .Data = Bytes(text)});
        }

        Write(path, Text(BuildFastFile(built)));
    }

    /// <summary>
    /// A multiplayer mission's file at <paramref name="path"/>: a packet file whose packet 0 is its settings, naming
    /// <paramref name="scenario"/> (as the game writes one).
    /// </summary>
    void WriteMission(const std::filesystem::path& path, std::string_view scenario)
    {
        const std::string settings = std::format("FITini\n[General]\nst PurchaseFile=\"netpur\"\n[Multiplayer]\n"
                                                 "st MissionName=\"{}\"\nFITend\n",
                                                 scenario);
        std::filesystem::create_directories(path.parent_path());
        MCTestContextScope scope;
        scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
        MCFileSystem::SetGameRoot(path.parent_path());
        MCPacketFile mission;
        REQUIRE_EQ(mission.Create(MCFileSystem::ToUtf8(path.filename())), 0);
        mission.Reserve(1);
        mission.WritePacket(0, std::span(reinterpret_cast<const uint8_t*>(settings.data()), settings.size()),
                            MCPacketStorage::Raw);
    }

    /// <summary>The original's game data in <paramref name="folder"/>, its five archives with their own words.</summary>
    void WriteGame(const std::filesystem::path& folder, std::string_view whose)
    {
        Write(folder / "SYSTEM.CFG", std::format("FITini\n; {}\nFITend\n", whose));
        const std::string art = std::format("{} art", whose);
        WriteArchive(folder / "ART.FST", {{"data\\art\\a.tga", art}, {"data\\art\\b.tga", art}});
        const std::string gameSystem = std::format("FITini\n; {} game system\nFITend\n", whose);
        const std::string profile = std::format("FITini\n; {} profile\nFITend\n", whose);
        WriteArchive(folder / "MISSION.FST", {{GameSystem, gameSystem}, {Profile, profile}});
        WriteArchive(folder / "MISC.FST", {{"data\\misc\\misc.txt", whose}});
        WriteArchive(folder / "SHAPES.FST", {{"data\\sprites\\cm.inf", "FITini\nFITend\n"}});
        WriteArchive(folder / "TERRAIN.FST", {{"data\\terrain\\base.dat", whose}});
        Write(folder / "data" / "objects" / "compbas.csv", std::format("1,{} laser,3\n", whose));
        Write(folder / "data" / "missions" / "loose.fit", std::format("{} loose", whose));
    }

    /// <summary>A launcher's map: its mission file and its map-only archive.</summary>
    void WriteMap(const std::filesystem::path& folder)
    {
        WriteMission(folder / "data" / "savegame" / "MCOMAP.MPK", "mcomap");
        WriteArchive(
            folder / "MCOMAP.FST",
            {{MapScenario, MapScenarioText}, {MapScript, "module mcomap;\n"}, {MapTerrain, "the map's cells"}});
    }

    /// <summary>
    /// A launcher's mod: A-MOD.FST, named before the original's archives, overriding a picture and the game system;
    /// ZMOD.FST, named after them, adding a picture and overriding nothing.
    /// </summary>
    void WriteMods(const std::filesystem::path& folder, std::string_view gameSystem = "FITini\n; the mod's\nFITend\n")
    {
        WriteArchive(folder / "A-MOD.FST", {{"data\\art\\a.tga", "the mod's art"}, {GameSystem, gameSystem}});
        WriteArchive(folder / "ZMOD.FST",
                     {{"data\\art\\b.tga", "the late mod's art"}, {"data\\art\\z.tga", "new art"}});
    }

    /// <summary>
    /// An install and a launcher's copy of the game, the launcher's with its maps and mods and its own copies of the
    /// original's files, which never play; and the launcher's user folder.
    /// </summary>
    struct MCMatchFolders
    {
        MCMatchFolders(std::string_view name)
            : Install(Group, std::string(name) + "-install")
            , Launcher(Group, std::string(name) + "-launcher")
            , User(Group, std::string(name) + "-user")
        {
            WriteGame(Install.Path(), "the install's");
            WriteMission(Install.Path() / "data" / "savegame" / "BOTH.MPK", "both");
            WriteMission(Install.Path() / "data" / "savegame" / "INSTALL.MPK", "install");
            WriteGame(Launcher.Path(), "the launcher's");
            WriteArchive(Launcher.Path() / "ART.FST", {{"data\\art\\a.tga", "the launcher's art"},
                                                       {"data\\art\\c.tga", "only the launcher's ART.FST"}});
            WriteMap(Launcher.Path());
            WriteMods(Launcher.Path());
            WriteMission(Launcher.Path() / "data" / "savegame" / "BOTH.MPK", "launcher both");
            Write(Launcher.Path() / "data" / "savegame" / "notes.txt", "not a map");
            Write(Launcher.Path() / "data" / "missions" / "extra.fit", "a loose file only the launcher has");
            Write(Launcher.Path() / "data" / "missions" / "mcomap.fit", "a loose copy of the map's scenario");
        }

        MCScratchFolder Install;
        MCScratchFolder Launcher;
        MCScratchFolder User;
    };

    /// <summary>
    /// The game's files set up as a launcher's match's start sets them (Main.cpp): the folders MCGameFolder::Played
    /// chooses for <paramref name="folders"/>, the game folder's lock held shared for the run, the launcher's user
    /// folder, and every FastFile opened as SystemInit opens them. Undone with the scope.
    /// </summary>
    class MCMatchFiles
    {
    public:
        explicit MCMatchFiles(const MCMatchFolders& folders, std::string_view commandLine = LauncherJoin)
        {
            const MCGameFolder::MCPlayed played =
                MCGameFolder::Played(*MakeDisk(), folders.Launcher.Path(), true, commandLine, folders.Install.Path());
            _Lock = MCUpdateLock::Open(played.Game, MCUpdateLock::GameLock);
            REQUIRE(_Lock != nullptr);
            REQUIRE(_Lock->TryShared());
            _Scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
            _Scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
            MCFileSystem::SetGameRoot(played.Game);
            MCFileSystem::SetUserRoot(folders.User.Path());
            MCFileSystem::SetLauncherRoot(played.Launcher);
            OpenGameFastFiles();
        }

    private:
        std::unique_ptr<MCUpdateLock> _Lock;
        MCTestContextScope _Scope;
    };

    /// <summary>
    /// Whether <paramref name="path"/> is the file at <paramref name="file"/> (on a disk that ignores case, the game takes
    /// a path as spelled when it's there).
    /// </summary>
    bool IsFile(const std::filesystem::path& path, const std::filesystem::path& file)
    {
        std::error_code error;
        return std::filesystem::equivalent(path, file, error);
    }

    /// <summary>The bytes the game reads for <paramref name="gamePath"/> (MCFile: loose, else the archives), or none.</summary>
    std::optional<std::string> GameFile(std::string_view gamePath)
    {
        MCFile file;

        if (file.Open(gamePath) != NO_ERR)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> bytes(file.GetLength());
        file.Read(bytes);
        return std::string(bytes.begin(), bytes.end());
    }

    /// <summary>The settings packet of mission file <paramref name="gamePath"/> as the game reads it, or none.</summary>
    std::optional<std::string> MissionSettings(std::string_view gamePath)
    {
        MCPacketFile mission;

        if (mission.Open(gamePath) != 0 || mission.SeekPacket(0) != 0)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(mission.GetPacketSize()));
        mission.ReadPacket(0, bytes);
        return std::string(bytes.begin(), bytes.end());
    }

    /// <summary>Whether <paramref name="settings"/> (a mission's settings packet) names <paramref name="scenario"/>.</summary>
    bool Names(const std::optional<std::string>& settings, std::string_view scenario)
    {
        return settings && settings->find(std::format("MissionName=\"{}\"", scenario)) != std::string::npos;
    }
}

/// <summary>
/// A launcher's match plays the install: --data named the launcher's own copy, the command line hosts or joins, and
/// the program is an install's that holds the game. Any other start plays the folder it found: a --data copy outside
/// a match, a start without --data, the install itself named, a program with no install. An installer's install whose
/// first start never finished leaves the launcher's copy to play as it is, without updates.
/// </summary>
TEST_CASE("launcher's match: a match a launcher starts with its own copy plays the program's install")
{
    const MCScratchFolder install(Group, "played-install");
    const MCScratchFolder launcher(Group, "played-launcher");
    Write(install.Path() / "SYSTEM.CFG", "FITini\nFITend\n");
    Write(launcher.Path() / "SYSTEM.CFG", "FITini\nFITend\n");
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

    for (const std::string_view line : {LauncherJoin, LauncherHost})
    {
        MCTest::Scope scope(line);
        const MCGameFolder::MCPlayed played = MCGameFolder::Played(*disk, launcher.Path(), true, line, install.Path());
        CHECK(played.Game == install.Path());
        CHECK(played.Launcher == launcher.Path());
        CHECK(played.Updates);
    }

    const auto plays =
        [&](const std::filesystem::path& found, bool named, std::string_view line, const std::filesystem::path& program)
    {
        const MCGameFolder::MCPlayed played = MCGameFolder::Played(*disk, found, named, line, program);
        return played.Launcher.empty() && played.Updates ? std::optional(played.Game) : std::nullopt;
    };

    CHECK(plays(launcher.Path(), true, "-mission 1", install.Path()) == launcher.Path());
    CHECK(plays(launcher.Path(), true, "", install.Path()) == launcher.Path());
    CHECK(plays(launcher.Path(), false, LauncherJoin, install.Path()) == launcher.Path());
    CHECK(plays(install.Path(), true, LauncherJoin, install.Path()) == install.Path());
    CHECK(plays(install.Path() / "." / "", true, LauncherJoin, install.Path()) == install.Path() / "." / "");
    CHECK(plays(launcher.Path(), true, LauncherJoin, std::filesystem::path()) == launcher.Path());

    // A program with no install beside it (a build folder's): the copy named is the game folder.
    const MCScratchFolder none(Group, "played-no-install");
    CHECK(plays(launcher.Path(), true, LauncherJoin, none.Path()) == launcher.Path());
    CHECK(plays(launcher.Path(), true, LauncherJoin, none.Path() / "missing") == launcher.Path());

    // An install an installer prepared, its first start not done: the launcher's copy as it is, no updates.
    REQUIRE(MCContentUpdater::Choose(*disk, none.Path(), {"game", "hd"}).has_value());
    REQUIRE(MCContentUpdater::AwaitsInstall(*disk, none.Path()));
    const MCGameFolder::MCPlayed waiting =
        MCGameFolder::Played(*disk, launcher.Path(), true, LauncherJoin, none.Path());
    CHECK(waiting.Game == launcher.Path());
    CHECK(waiting.Launcher.empty());
    CHECK(!waiting.Updates);
}

/// <summary>
/// The launcher's maps and mods are its community content, by MechCommander Online's rules: an archive in the folder's
/// root but the original's five, a mission in data\savegame\. Nothing else of a launcher's folder is.
/// </summary>
TEST_CASE("launcher's match: a map or mod is an archive beside the original's five, or a mission in data\\savegame")
{
    for (const std::string_view content : {"A-MOD.FST", "mcomap.fst", ".\\zmod.Fst", "data\\savegame\\MCOMAP.MPK",
                                           "DATA/SAVEGAME/duel.mpk", ".\\data\\savegame\\x.mpk"})
    {
        MCTest::Scope scope(content);
        CHECK(MCFileSystem::IsLauncherContent(content));
    }

    for (const std::string_view other : {"ART.FST",
                                         "art.fst",
                                         "Mission.fst",
                                         "MISC.FST",
                                         "SHAPES.FST",
                                         "TERRAIN.FST",
                                         "data\\mod.fst",
                                         ".fst",
                                         "data\\savegame\\x.sav",
                                         "data\\savegame\\sub\\x.mpk",
                                         "data\\missions\\x.mpk",
                                         "x.mpk",
                                         "system.cfg",
                                         "data\\objects\\compbas.csv",
                                         "..\\mod.fst",
                                         "data\\..\\mod.fst",
                                         "C:\\mod.fst",
                                         "\\mod.fst",
                                         "/mod.fst",
                                         ""})
    {
        MCTest::Scope scope(other);
        CHECK(!MCFileSystem::IsLauncherContent(other));
    }
}

/// <summary>
/// The game of a launcher's match reads the install's game with the launcher's maps and mods: the launcher's map
/// (its mission and its map-only archive) and mods play; the archives are one set in name order, each file read from
/// the first that holds it, as MCX.EXE searches its own folder's (A-MOD.FST overrides the original's five, ZMOD.FST
/// only adds); a mission in both folders plays from the launcher's; the launcher's own copies of the original's five
/// archives, SYSTEM.CFG and loose data never play, nor its other files.
/// </summary>
TEST_CASE("launcher's match: its maps and mods play beside the install, a mod's archive before the ones named after it")
{
    const MCMatchFolders folders("lookup");
    const MCMatchFiles files(folders);

    CHECK((MCFileSystem::FindFiles("*.fst") == std::vector<std::string>{"A-MOD.FST", "ART.FST", "MCOMAP.FST",
                                                                        "MISC.FST", "MISSION.FST", "SHAPES.FST",
                                                                        "TERRAIN.FST", "ZMOD.FST"}));
    CHECK(IsFile(MCFileSystem::Resolve("ART.FST"), folders.Install.Path() / "ART.FST"));
    CHECK(IsFile(MCFileSystem::Resolve("a-mod.fst"), folders.Launcher.Path() / "A-MOD.FST"));
    CHECK(IsFile(MCFileSystem::Resolve("system.cfg"), folders.Install.Path() / "SYSTEM.CFG"));

    // The mods: A-MOD.FST before the original's archives, ZMOD.FST after them.
    CHECK(GameFile("data\\art\\a.tga") == "the mod's art");
    CHECK(GameFile("data\\art\\b.tga") == "the install's art");
    CHECK(GameFile("data\\art\\z.tga") == "new art");
    CHECK(GameFile(GameSystem) == "FITini\n; the mod's\nFITend\n");
    CHECK(GameFile(Profile) == "FITini\n; the install's profile\nFITend\n");
    CHECK(GameFile("data\\terrain\\base.dat") == "the install's");

    // The map: its mission file and the map-only archive's files (the launcher's loose copy of its scenario isn't a
    // map file, so the archive's plays).
    CHECK(Names(MissionSettings(MapMission), "mcomap"));
    CHECK(GameFile(MapScenario) == std::string(MapScenarioText));
    CHECK(GameFile(MapTerrain) == "the map's cells");
    CHECK(Names(MissionSettings("data\\savegame\\BOTH.MPK"), "launcher both"));
    CHECK(Names(MissionSettings("data\\savegame\\INSTALL.MPK"), "install"));

    // The launcher's own copy of the original's files never plays, nor its files that are no map or mod.
    CHECK(!GameFile("data\\art\\c.tga"));
    CHECK(GameFile(Objects) == "1,the install's laser,3\n");
    CHECK(GameFile("data\\missions\\loose.fit") == "the install's loose");
    CHECK(GameFile("system.cfg") == "FITini\n; the install's\nFITend\n");
    CHECK(!GameFile("data\\missions\\extra.fit"));
    CHECK(!MCFileSystem::Exists("data\\savegame\\notes.txt"));

    // The multiplayer missions listed, each with the folders that have it.
    const std::vector<MCFoundFile> missions = MCFileSystem::FindFileSources("data\\savegame\\*.mpk");
    REQUIRE_EQ(missions.size(), size_t{3});
    CHECK_EQ(missions[0].Name, std::string("BOTH.MPK"));
    CHECK((missions[0].InLauncherFolder && missions[0].InGameFolder && !missions[0].InUserFolder));
    CHECK_EQ(missions[1].Name, std::string("INSTALL.MPK"));
    CHECK((!missions[1].InLauncherFolder && missions[1].InGameFolder));
    CHECK_EQ(missions[2].Name, std::string("MCOMAP.MPK"));
    CHECK((missions[2].InLauncherFolder && !missions[2].InGameFolder));
    CHECK((MCFileSystem::FindFiles("data\\savegame\\*.*") ==
           std::vector<std::string>{"BOTH.MPK", "INSTALL.MPK", "MCOMAP.MPK"}));

    // A mission the game wrote itself (one a host sent) comes first, from the user folder.
    WriteMission(folders.User.Path() / "data" / "savegame" / "MCOMAP.MPK", "sent by the host");
    CHECK(Names(MissionSettings(MapMission), "sent by the host"));
}

/// <summary>
/// Nothing the game does in a launcher's match reaches the launcher's folder: the start's lock is the install's, and a
/// map's mission readied for update, a rename, a copy, a delete, a new folder and new files all happen in the user
/// folder (a map's file copied there first); the launcher's copies stay as they were, byte for byte, and its map plays
/// on. The install's update stays its own (ContentUpdaterTests). (The game opens a file to read it or to write it anew,
/// never to change it in place.)
/// </summary>
TEST_CASE("launcher's match: nothing the game writes reaches the launcher's folder")
{
    const MCMatchFolders folders("writes");
    const std::map<std::string, std::string> launcherBefore = FolderContents(folders.Launcher.Path());
    const std::map<std::string, std::string> installBefore = FolderContents(folders.Install.Path());
    {
        const MCMatchFiles files(folders);
        const std::filesystem::path user = folders.User.Path() / "data" / "savegame";

        const std::filesystem::path updated = MCFileSystem::ResolveWrite(MapMission, true);
        CHECK(IsFile(updated, user / "MCOMAP.MPK"));
        CHECK(Read(updated) == Read(folders.Launcher.Path() / "data" / "savegame" / "MCOMAP.MPK"));
        CHECK(MCFileSystem::RenameFile("data\\savegame\\BOTH.MPK", "data\\savegame\\RENAMED.MPK"));
        CHECK(Names(MissionSettings("data\\savegame\\RENAMED.MPK"), "launcher both"));
        CHECK(Names(MissionSettings("data\\savegame\\BOTH.MPK"), "launcher both"));
        CHECK(MCFileSystem::CopyFile("ZMOD.FST", "COPY.FST"));
        CHECK(Read(folders.User.Path() / "COPY.FST") == Read(folders.Launcher.Path() / "ZMOD.FST"));
        CHECK(MCFileSystem::RemoveFile(MapMission));
        CHECK(!MCFileSystem::RemoveFile("A-MOD.FST"));
        CHECK(MCFileSystem::Exists(MapMission));
        CHECK(MCFileSystem::MakeDirectory("data\\savegame\\new"));

        MCFile created;
        REQUIRE_EQ(created.Create("A-MOD.FST"), NO_ERR);
        created.Write(Bytes("the game's own"));
        created.Close();
        CHECK(Read(folders.User.Path() / "A-MOD.FST") == "the game's own");
        CHECK(IsFile(MCFileSystem::Resolve("A-MOD.FST"), folders.User.Path() / "A-MOD.FST"));
    }

    CHECK(FolderContents(folders.Launcher.Path()) == launcherBefore);
    // The install gains its lock's file alone.
    std::map<std::string, std::string> installAfter = FolderContents(folders.Install.Path());
    std::erase_if(installAfter, [](const auto& file) { return file.first.starts_with(MCContentList::UpdateFolder); });
    CHECK(installAfter == installBefore);
}

/// <summary>
/// A room checks what plays (network/MCGameDataHash.h): in a launcher's match, the data hash reads the files as the
/// game finds them, the same as a single folder holding the install's game with the launcher's map and mods. A mod's
/// game system changes it; the launcher's own copies of the original's files and loose data, which never play, don't.
/// </summary>
TEST_CASE("launcher's match: the data hash covers the files as the game finds them")
{
    const auto hash = [](const MCMatchFolders& folders)
    {
        const MCMatchFiles files(folders);
        return MCMissionDataHash(MapMission);
    };

    const MCMatchFolders folders("hash");
    const uint32_t match = hash(folders);
    CHECK(match != 0u);

    // One folder holding what the match plays: the install's game, the launcher's map and mods.
    const MCScratchFolder merged(Group, "hash-merged");
    WriteGame(merged.Path(), "the install's");
    WriteMap(merged.Path());
    WriteMods(merged.Path());
    {
        MCTestContextScope scope;
        scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
        scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
        MCFileSystem::SetGameRoot(merged.Path());
        OpenGameFastFiles();
        CHECK_EQ(MCMissionDataHash(MapMission), match);
    }

    // The launcher's own gameplay files that never play (a loose game system, its MISSION.FST) change nothing.
    const MCMatchFolders loose("hash-loose");
    Write(loose.Launcher.Path() / "data" / "missions" / "gamesys.fit", "FITini\n; a loose game system\nFITend\n");
    WriteArchive(loose.Launcher.Path() / "MISSION.FST", {{Profile, "FITini\n; another profile\nFITend\n"}});
    CHECK_EQ(hash(loose), match);

    // A mod with another game system changes it.
    const MCMatchFolders modded("hash-modded");
    WriteMods(modded.Launcher.Path(), "FITini\n; another mod's\nFITend\n");
    CHECK(hash(modded) != match);
}
