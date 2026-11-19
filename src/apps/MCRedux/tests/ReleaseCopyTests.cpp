#include "stdafx.h"
#include "MCContentFixture.h"
#include "MCFastFileFixture.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "TestGame.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFile.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCPacketFile.h"
#include "lib/MCReleaseData.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCSystemConfig.h"
#include "mission/MCMission.h"
#include "object/MCMasterComponent.h"
#include "network/MCGameDataHash.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCFileSystem.h"
#include "update/MCInstalled.h"
#include "update/MCReleaseCopy.h"
#include "update/MCUpdateDisk.h"

// Redux HD's own copy of a match's data (update/MCReleaseCopy.h, lib/MCReleaseData.h): each start fills it from the
// game folder's release files, and a match reads what it holds only from it, so a mod in the game folder (an archive
// beside the stock five, a loose file, a stock archive written over) changes single player but never a match.

namespace
{
    using namespace MCContentFixture;
    using namespace MCFastFileFixture;

    constexpr std::string_view Group = "mc_release_copy_tests";

    constexpr std::string_view GameSystem = "data\\missions\\gamesys.fit";
    constexpr std::string_view Profile = "data\\missions\\profiles\\PM100100.fit";
    constexpr std::string_view Pilot = "data\\missions\\warriors\\pilot1.fit";
    constexpr std::string_view Components = "data\\objects\\compbas.csv";
    constexpr std::string_view Picture = "data\\art\\a.tga";
    constexpr std::string_view ClanShop = "data\\missions\\clanpur.fit";

    /// <summary>The paths the release lists, as its content list spells them, and their text.</summary>
    const std::vector<std::pair<std::string, std::string>> LooseFiles = {
        {"SYSTEM.CFG", "FITini\nFITend\n"},
        {"DATA/OBJECTS/COMPBAS.CSV", "1,the release's laser,3\n"},
        {"DATA/SAVEGAME/STOCK.MPK", "the release's map"},
        {"DATA/SPRITES/MECHS.PAK", "the release's art"},
    };

    std::vector<uint8_t> Bytes(std::string_view text)
    {
        return {text.begin(), text.end()};
    }

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
    /// A multiplayer mission's file at <paramref name="path"/> naming <paramref name="scenario"/> (as the game writes
    /// one: a packet file whose packet 0 is its settings).
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

    /// <summary>The release's files in <paramref name="folder"/>: its five archives and its loose files.</summary>
    void WriteRelease(const std::filesystem::path& folder)
    {
        WriteArchive(folder / "ART.FST", {{Picture, "the release's art"}});
        WriteArchive(folder / "MISSION.FST", {{GameSystem, "FITini\n; the release's game system\nFITend\n"},
                                              {Profile, "FITini\n; the release's profile\nFITend\n"},
                                              {Pilot, "FITini\n; the release's pilot\nFITend\n"},
                                              {ClanShop, "FITini\n; the release's shop\nFITend\n"}});
        WriteArchive(folder / "MISC.FST", {{"data\\misc\\misc.txt", "the release's"}});
        WriteArchive(folder / "SHAPES.FST", {{"data\\sprites\\cm.inf", "FITini\nFITend\n"}});
        WriteArchive(folder / "TERRAIN.FST", {{"data\\terrain\\base.dat", "the release's cells"}});

        for (const auto& [path, text] : LooseFiles)
        {
            Write(folder / MCFileSystem::FromUtf8(path), text);
        }
    }

    /// <summary>The installed record of the release in <paramref name="folder"/>: every file there, as it is now.</summary>
    MCInstalled::MCRecord RecordOf(const std::filesystem::path& folder)
    {
        MCInstalled::MCRecord record{.Release = 9};
        const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
        const auto files = disk->Files(folder);
        REQUIRE(files.has_value());

        for (const std::string& path : *files)
        {
            const std::filesystem::path full = folder / MCFileSystem::FromUtf8(path);
            const auto hash = disk->Hash(full);
            REQUIRE(hash.has_value());
            const MCUpdateDisk::MCStat stat = disk->Stat(full);
            record.Files.insert_or_assign(MCContentList::FoldedPath(path),
                                          MCInstalled::MCFile{.Path = path,
                                                              .Set = std::string(MCContentList::GameSet),
                                                              .Sha256 = *hash,
                                                              .Size = stat.Size,
                                                              .Written = stat.Written});
        }

        return record;
    }

    /// <summary>
    /// The release installed in <paramref name="folder"/> as a start leaves it: its record written
    /// (<c>installed.json</c>, every file there as it is now) and the copy filled.
    /// </summary>
    MCInstalled::MCRecord InstallRelease(const std::filesystem::path& folder)
    {
        const MCInstalled::MCRecord record = RecordOf(folder);
        Write(folder / MCFileSystem::FromUtf8(MCInstalled::RecordPath), MCInstalled::Text(record));
        REQUIRE(MCReleaseCopy::Sync(*MakeDisk(), folder, record).has_value());
        return record;
    }

    std::filesystem::path InCopy(const std::filesystem::path& game, std::string_view path)
    {
        return MCReleaseCopy::FolderIn(game) / MCFileSystem::FromUtf8(path);
    }

    /// <summary>
    /// The game's files as a start sets them: <paramref name="game"/>'s archives and its copy opened, the user folder
    /// <paramref name="user"/>. The copy is closed and no match played after the scope.
    /// </summary>
    class MCGameFiles
    {
    public:
        MCGameFiles(const std::filesystem::path& game, const std::filesystem::path& user)
        {
            _Scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
            _Scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
            MCFileSystem::SetGameRoot(game);
            MCFileSystem::SetUserRoot(user);
            OpenGameFastFiles();
            // As system.cfg names them (SystemInit reads it).
            WarriorPath = "data\\missions\\warriors\\";
            ProfilePath = "data\\missions\\profiles\\";
            ShapesPath = "data\\sprites\\";
            TerrainPath = "data\\terrain\\";
        }

        ~MCGameFiles()
        {
            WarriorPath = _Warriors;
            ProfilePath = _Profiles;
            ShapesPath = _Shapes;
            TerrainPath = _Terrain;
            MCGameContext::Current().SetMultiPlayer(nullptr);
            MCFilesAhead::Forget();
            MCReleaseData::Close();
        }

        MCGameFiles(const MCGameFiles&) = delete;
        MCGameFiles& operator=(const MCGameFiles&) = delete;

    private:
        std::string _Warriors = WarriorPath;
        std::string _Profiles = ProfilePath;
        std::string _Shapes = ShapesPath;
        std::string _Terrain = TerrainPath;
        MCTestContextScope _Scope;
    };

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

    bool Says(const std::optional<std::string>& text, std::string_view words)
    {
        return text.has_value() && text->find(words) != std::string::npos;
    }
}

TEST_CASE("release copy: a start fills it from the game folder's release files, the stock archives and the objects")
{
    const MCScratchFolder game(Group, "fill");
    WriteRelease(game.Path());
    const MCInstalled::MCRecord record = RecordOf(game.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

    const auto synced = MCReleaseCopy::Sync(*disk, game.Path(), record);
    REQUIRE(synced.has_value());
    CHECK_EQ(synced->Held, 7u);
    CHECK_EQ(synced->Filled, 7u);
    CHECK(synced->Lacking.empty());

    for (const std::string_view held : {"ART.FST", "MISSION.FST", "MISC.FST", "SHAPES.FST", "TERRAIN.FST",
                                        "DATA/OBJECTS/COMPBAS.CSV", "DATA/SAVEGAME/STOCK.MPK"})
    {
        MCTest::Scope scope(held);
        CHECK_EQ(Read(InCopy(game.Path(), held)), Read(game.Path() / MCFileSystem::FromUtf8(held)));
    }

    // Not what a match plays by: the settings, the art's own files.
    CHECK(!(std::filesystem::exists(InCopy(game.Path(), "SYSTEM.CFG"))));
    CHECK(!(std::filesystem::exists(InCopy(game.Path(), "DATA/SPRITES/MECHS.PAK"))));

    // The next start finds it as it was: nothing filled or hashed.
    const auto again = MCReleaseCopy::Sync(*disk, game.Path(), record);
    REQUIRE(again.has_value());
    CHECK_EQ(again->Filled, 0u);
    CHECK_EQ(again->Hashed, 0u);

    // A copy's file whose time alone changed (a restore, an install copied): hashed once, recorded as it is now, so the
    // next start neither hashes it again nor takes it as changed.
    const std::filesystem::path art = InCopy(game.Path(), "ART.FST");
    std::filesystem::last_write_time(art, std::filesystem::last_write_time(art) - std::chrono::hours(1));
    const auto touched = MCReleaseCopy::Sync(*disk, game.Path(), record);
    REQUIRE(touched.has_value());
    CHECK_EQ(touched->Filled, 0u);
    CHECK_EQ(touched->Hashed, 1u);
    const auto settled = MCReleaseCopy::Sync(*disk, game.Path(), record);
    REQUIRE(settled.has_value());
    CHECK_EQ(settled->Hashed, 0u);
    {
        Write(game.Path() / MCFileSystem::FromUtf8(MCInstalled::RecordPath), MCInstalled::Text(record));
        const MCScratchFolder user(Group, "fill-user");
        const MCGameFiles files(game.Path(), user.Path());
        CHECK(MCReleaseData::Has());
        CHECK(MCReleaseData::Whole());
    }

    // A mod written over a stock archive since: the copy keeps the release's.
    WriteArchive(game.Path() / "MISSION.FST", {{GameSystem, "FITini\n; the mod's game system\nFITend\n"}});
    const auto modded = MCReleaseCopy::Sync(*disk, game.Path(), record);
    REQUIRE(modded.has_value());
    CHECK_EQ(modded->Filled, 0u);
    CHECK(modded->Lacking.empty());

    // A copy's file changed is filled again from the game folder's, still the release's.
    Write(InCopy(game.Path(), "DATA/OBJECTS/COMPBAS.CSV"), "1,a changed laser,3\n");
    const auto refilled = MCReleaseCopy::Sync(*disk, game.Path(), record);
    REQUIRE(refilled.has_value());
    CHECK_EQ(refilled->Filled, 1u);
    CHECK_EQ(Read(InCopy(game.Path(), "DATA/OBJECTS/COMPBAS.CSV")), "1,the release's laser,3\n");

    // A copy's file gone, the game folder's a mod: the copy lacks it.
    std::filesystem::remove(InCopy(game.Path(), "MISSION.FST"));
    const auto lacking = MCReleaseCopy::Sync(*disk, game.Path(), record);
    REQUIRE(lacking.has_value());
    CHECK(lacking->Lacking == std::vector<std::string>{"MISSION.FST"});
    CHECK_EQ(lacking->Held, 6u);

    // A file the release no longer lists goes from the copy.
    MCInstalled::MCRecord fewer = record;
    fewer.Files.erase(MCContentList::FoldedPath("TERRAIN.FST"));
    REQUIRE(MCReleaseCopy::Sync(*disk, game.Path(), fewer).has_value());
    CHECK(!(std::filesystem::exists(InCopy(game.Path(), "TERRAIN.FST"))));

    // No release installed (a developer's data): no copy is made.
    const MCScratchFolder bare(Group, "fill-bare");
    WriteRelease(bare.Path());
    const auto none = MCReleaseCopy::Sync(*disk, bare.Path(), MCInstalled::MCRecord{});
    REQUIRE(none.has_value());
    CHECK_EQ(none->Held, 0u);
    CHECK(!(std::filesystem::exists(MCReleaseCopy::FolderIn(bare.Path()))));
}

TEST_CASE(
    "release copy: a match reads the release's data, single player the game folder's mods; additions play in both")
{
    const MCScratchFolder game(Group, "reads");
    const MCScratchFolder user(Group, "reads-user");
    WriteRelease(game.Path());
    InstallRelease(game.Path());

    // The mods: an archive named before the stock ones, a stock archive written over, a loose file in the user
    // folder; and what they add: a pilot and a map.
    WriteArchive(game.Path() / "AA_MOD.FST", {{GameSystem, "FITini\n; the mod's game system\nFITend\n"},
                                              {Picture, "the mod's art"},
                                              {"data\\missions\\warriors\\newpilot.fit", "the mod's new pilot"}});
    WriteArchive(game.Path() / "MISSION.FST",
                 {{Profile, "FITini\n; the mod's profile\nFITend\n"}, {Pilot, "FITini\n; the mod's pilot\nFITend\n"}});
    Write(user.Path() / "data" / "objects" / "compbas.csv", "1,the mod's laser,3\n");
    WriteArchive(game.Path() / "FANMAP.FST", {{"data\\missions\\fanmap.fit", "the fan map's scenario"}});

    const MCGameFiles files(game.Path(), user.Path());
    REQUIRE(MCReleaseData::Has());
    CHECK(MCReleaseData::Whole());

    const auto playsMods = [&]
    {
        CHECK(Says(GameFile(GameSystem), "the mod's game system"));
        CHECK(Says(GameFile(Profile), "the mod's profile"));
        CHECK(Says(GameFile(Pilot), "the mod's pilot"));
        CHECK(Says(GameFile(Components), "the mod's laser"));
        CHECK(Says(GameFile(Picture), "the mod's art"));
    };

    playsMods();
    const uint32_t before = MCReleaseData::Switches();

    MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
    CHECK(MCReleaseData::Playing());
    CHECK_EQ(MCReleaseData::Switches(), before + 1);
    CHECK(Says(GameFile(GameSystem), "the release's game system"));
    CHECK(Says(GameFile(Profile), "the release's profile"));
    CHECK(Says(GameFile(Pilot), "the release's pilot"));
    CHECK(Says(GameFile(Components), "the release's laser"));
    CHECK(Says(GameFile(Picture), "the release's art"));
    CHECK(Says(GameFile(ClanShop), "the release's shop"));
    // What a mod or a fan adds beside the release's plays.
    CHECK(Says(GameFile("data\\missions\\warriors\\newpilot.fit"), "the mod's new pilot"));
    CHECK(Says(GameFile("data\\missions\\fanmap.fit"), "the fan map's scenario"));

    // Read ahead, a file the copy holds is left for MCFile::Open, which reads the copy's.
    MCFilesAhead::ReadAhead({std::string(GameSystem)}, MCGameContext::Current().FastFiles());
    CHECK(Says(GameFile(GameSystem), "the release's game system"));

    // Single player again.
    MCGameContext::Current().SetMultiPlayer(nullptr);
    CHECK(!(MCReleaseData::Playing()));
    CHECK_EQ(MCReleaseData::Switches(), before + 2);
    playsMods();
}

TEST_CASE("release copy: a match's data hash is the release's whatever mods the game folder holds")
{
    const auto matchHash = [](const std::filesystem::path& game, const std::filesystem::path& user)
    {
        const MCGameFiles files(game, user);
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        return std::pair(MCSharedDataHash(), MCSharedDataFiles());
    };

    const MCScratchFolder clean(Group, "hash-clean");
    const MCScratchFolder cleanUser(Group, "hash-clean-user");
    WriteRelease(clean.Path());
    InstallRelease(clean.Path());
    const auto [cleanHash, cleanFiles] = matchHash(clean.Path(), cleanUser.Path());

    const MCScratchFolder modded(Group, "hash-modded");
    const MCScratchFolder moddedUser(Group, "hash-modded-user");
    WriteRelease(modded.Path());
    InstallRelease(modded.Path());
    WriteArchive(modded.Path() / "AA_MOD.FST", {{GameSystem, "FITini\n; the mod's game system\nFITend\n"},
                                                {"data\\missions\\warriors\\newpilot.fit", "the mod's new pilot"}});
    WriteArchive(modded.Path() / "MISSION.FST", {{Pilot, "FITini\n; the mod's pilot\nFITend\n"}});
    Write(moddedUser.Path() / "data" / "objects" / "compbas.csv", "1,the mod's laser,3\n");
    const auto [moddedHash, moddedFiles] = matchHash(modded.Path(), moddedUser.Path());

    CHECK(moddedFiles == cleanFiles);
    CHECK_EQ(moddedHash, cleanHash);

    // Out of a match the mods count, as single player plays them.
    {
        const MCGameFiles files(modded.Path(), moddedUser.Path());
        CHECK(MCSharedDataHash() != cleanHash);
    }
}

TEST_CASE("release copy: the component list kept across missions is read again when a match begins and ends")
{
    // The release's game system and components (MISSION.FST, MISC.FST), and a mod renaming a weapon.
    const MCScratchFolder game(Group, "components");
    const MCScratchFolder user(Group, "components-user");

    if (!MCTestGame::Available())
    {
        return;
    }

    const std::filesystem::path stock = MCFileSystem::FromUtf8(MCTest::Option("game").value_or("."));

    for (const std::string_view archive : {"MISSION.FST", "MISC.FST"})
    {
        std::filesystem::copy_file(stock / archive, game.Path() / archive);
    }

    InstallRelease(game.Path());
    const MCGameFiles files(game.Path(), user.Path());
    const std::optional<std::string> release = GameFile(Components);
    REQUIRE(release.has_value());
    constexpr std::string_view Weapon = "Laser";
    constexpr std::string_view Renamed = "Lazer";
    std::string modded = *release;

    for (size_t at = modded.find(Weapon); at != std::string::npos; at = modded.find(Weapon, at + Renamed.size()))
    {
        modded.replace(at, Weapon.size(), Renamed);
    }

    REQUIRE(modded != *release);
    Write(user.Path() / "data" / "objects" / "compbas.csv", modded);

    const auto named = [](std::string_view word)
    {
        return std::ranges::any_of(MasterComponentList, [word](const MCMasterComponent& component)
                                   { return component.Name.find(word) != std::string::npos; });
    };

    MasterComponentList.clear();
    LoadComponentListIfSwitched();
    CHECK(named(Renamed));
    CHECK(!named(Weapon));

    MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
    LoadComponentListIfSwitched();
    CHECK(named(Weapon));
    CHECK(!named(Renamed));

    MCGameContext::Current().SetMultiPlayer(nullptr);
    LoadComponentListIfSwitched();
    CHECK(named(Renamed));

    // Later tests read the game's own.
    MasterComponentList.clear();
}

TEST_CASE("release copy: in a match a fan map's own files, which the copy doesn't hold, count in its data hash")
{
    constexpr std::string_view FanMission = "data\\savegame\\FANMAP.MPK";
    const MCScratchFolder game(Group, "fan-map");
    const MCScratchFolder user(Group, "fan-map-user");
    WriteRelease(game.Path());
    InstallRelease(game.Path());
    WriteMission(game.Path() / "data" / "savegame" / "FANMAP.MPK", "fanmap");
    Write(game.Path() / "data" / "missions" / "fanmap.fit",
          "FITini\n[TerrainSystem]\nst TerrainFileName = \"fanmap\"\nFITend\n");
    Write(game.Path() / "data" / "terrain" / "fanmap.dat", "the fan map's cells");

    const auto matchHash = [&]
    {
        const MCGameFiles files(game.Path(), user.Path());
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        return std::pair(MCMissionDataHash(FanMission), MCMissionDataFiles(FanMission));
    };

    const auto [hash, mapFiles] = matchHash();
    CHECK(std::ranges::find(mapFiles, std::string("data\\terrain\\fanmap.dat")) != mapFiles.end());
    Write(game.Path() / "data" / "terrain" / "fanmap.dat", "another version's cells");
    CHECK(matchHash().first != hash);
}

TEST_CASE(
    "release copy: a copy filled for another release than the one installed isn't read: a match reads the game folder")
{
    const MCScratchFolder game(Group, "other-release");
    const MCScratchFolder user(Group, "other-release-user");
    WriteRelease(game.Path());
    MCInstalled::MCRecord record = InstallRelease(game.Path());
    WriteArchive(game.Path() / "AA_MOD.FST", {{GameSystem, "FITini\n; the mod's game system\nFITend\n"}});

    {
        const MCGameFiles files(game.Path(), user.Path());
        REQUIRE(MCReleaseData::Has());
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        CHECK(Says(GameFile(GameSystem), "the release's game system"));
    }

    // An update's release in place, its fill not finished: the copy still names the one before.
    record.Release += 1;
    Write(game.Path() / MCFileSystem::FromUtf8(MCInstalled::RecordPath), MCInstalled::Text(record));
    {
        const MCGameFiles files(game.Path(), user.Path());
        CHECK(!MCReleaseData::Has());
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        CHECK(!MCReleaseData::Playing());
        CHECK(Says(GameFile(GameSystem), "the mod's game system"));
    }

    // The next start fills it for the release installed.
    REQUIRE(MCReleaseCopy::Sync(*MakeDisk(), game.Path(), record).has_value());
    {
        const MCGameFiles files(game.Path(), user.Path());
        CHECK(MCReleaseData::Has());
    }
}
