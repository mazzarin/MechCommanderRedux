#include "stdafx.h"
#include "../MCGameFolder.h"
#include "MCTest.h"
#include "platform/MCUtf8Path.h"
#include "update/MCContentSwap.h"
#include "update/MCUpdateLock.h"

namespace
{
    /// <summary>A scratch folder of the test's own under the temp folder, deleted with it.</summary>
    class ScratchFolder
    {
    public:
        explicit ScratchFolder(std::string_view name)
            : _Path(std::filesystem::temp_directory_path() / "mc_game_folder_tests" / name)
        {
            std::error_code error;
            std::filesystem::remove_all(_Path, error);
            std::filesystem::create_directories(_Path);
        }

        ~ScratchFolder()
        {
            std::error_code error;
            std::filesystem::remove_all(_Path, error);
        }

        const std::filesystem::path& Path() const { return _Path; }

    private:
        std::filesystem::path _Path;
    };

    /// <summary>Makes <paramref name="folder"/> look like an install: its SYSTEM.CFG and, unless not, MCX.EXE.</summary>
    std::filesystem::path MakeInstall(const std::filesystem::path& folder, bool withExe = true,
                                      std::string_view config = "SYSTEM.CFG", std::string_view exe = "MCX.EXE")
    {
        std::filesystem::create_directories(folder);
        std::ofstream(folder / config) << "FITini\n";

        if (withExe)
        {
            std::ofstream(folder / exe) << "MZ";
        }

        return folder;
    }

    /// <summary>The whole of a small file, or empty when it can't be read.</summary>
    std::string ReadAll(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
}

/// <summary>
/// The search finds installs (SYSTEM.CFG beside MCX.EXE, in any case) in a folder, its subfolders, and the C: drive of
/// a Wine wrapper; not mods inside an install, Windows' or the user profiles' folders, other apps, folders past the
/// depth, or folders reached through a link.
/// </summary>
TEST_CASE("launcher: the game is found in folders and Wine wrappers, not in mods, profiles or links")
{
    const ScratchFolder scratch("search");
    const std::filesystem::path& root = scratch.Path();
    const std::filesystem::path wrapperDrive = root / "MechCommander Gold.app" / "Contents" / "Resources" / "drive_c";
    const std::filesystem::path wrapped = MakeInstall(wrapperDrive / "Program Files" / "MicroProse" / "MCX");
    MakeInstall(wrapped / "MCG Darkest Hours");
    MakeInstall(wrapperDrive / "windows" / "MCX");
    MakeInstall(wrapperDrive / "users" / "player" / "Desktop" / "MCX");
    const std::filesystem::path kegworks = MakeInstall(root / "Games" / "Kegworks.app" / "Contents" / "SharedSupport" /
                                                       "prefix" / "drive_c" / "GOG Games" / "MechCommander Gold");
    MakeInstall(root / "Other.app" / "Game");
    const std::filesystem::path lowercase = MakeInstall(root / "Games" / "MCG", true, "system.cfg", "mcx.exe");
    MakeInstall(root / "No Exe", false);
    MakeInstall(root / "Deep" / "er" / "MCX");

    std::error_code linkError;
    std::filesystem::create_directory_symlink(root / "Games", root / "Link", linkError);

    const std::vector<std::filesystem::path> found = MCGameFolder::FindInstalls(root, 2);
    std::vector<std::filesystem::path> expected = {kegworks, lowercase, wrapped};
    std::ranges::sort(expected);
    REQUIRE_EQ(found.size(), expected.size());

    for (size_t i = 0; i < found.size(); i++)
    {
        CHECK_EQ(MCFileSystem::ToUtf8(found[i]), MCFileSystem::ToUtf8(expected[i]));
    }

    // A wrapper itself, as the player might choose it.
    const std::vector<std::filesystem::path> inWrapper = MCGameFolder::FindInstalls(root / "MechCommander Gold.app", 0);
    REQUIRE_EQ(inWrapper.size(), size_t{1});
    CHECK_EQ(MCFileSystem::ToUtf8(inWrapper.front()), MCFileSystem::ToUtf8(wrapped));

    CHECK(MCGameFolder::HasGameData(lowercase));
    CHECK(MCGameFolder::HasGameData(root / "No Exe"));
    CHECK(!MCGameFolder::IsInstall(root / "No Exe"));
    CHECK(!MCGameFolder::HasGameData(root / "Missing"));
}

/// <summary>
/// A swap stopped midway sets SYSTEM.CFG aside before the new one moves in: a folder whose update is under way (ready,
/// begun or being repaired) is still the game's, so the start finds it again and finishes the swap; the update folder
/// alone (its locks) isn't.
/// </summary>
TEST_CASE("launcher: a folder whose update is under way is the game's, its SYSTEM.CFG set aside")
{
    const ScratchFolder scratch("midway");

    for (const std::string_view under :
         {MCContentSwap::JournalFile, MCContentSwap::ReadyFile, MCContentSwap::RepairFile})
    {
        const std::filesystem::path folder = scratch.Path() / std::filesystem::path(under).filename();
        const std::filesystem::path file = folder / std::filesystem::path(under);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << "{}";
        std::ofstream(folder / "MCX.EXE") << "MZ";
        CHECK(MCGameFolder::HasGameData(folder));
        CHECK(MCGameFolder::IsInstall(folder));
    }

    const std::filesystem::path locksOnly = scratch.Path() / "locks";
    CHECK(MCUpdateLock::Open(locksOnly, MCUpdateLock::GameLock) != nullptr);
    std::ofstream(locksOnly / "MCX.EXE") << "MZ";
    CHECK(!MCGameFolder::HasGameData(locksOnly));
    CHECK(!MCGameFolder::IsInstall(locksOnly));
}

/// <summary>The chosen folder is remembered in the user folder, accented letters and all; none is empty.</summary>
TEST_CASE("launcher: the chosen game folder is remembered for the next start")
{
    const ScratchFolder scratch("remember");
    const std::filesystem::path user = scratch.Path() / "user";
    CHECK(MCGameFolder::Remembered(user).empty());

    const std::filesystem::path game = scratch.Path() / std::filesystem::path(u8"Jeux vidéo") / "MechCommander Gold";
    CHECK(MCGameFolder::Remember(user, game).empty());
    CHECK(MCGameFolder::Remembered(user) == game);

    const std::filesystem::path other = scratch.Path() / "MCG";
    CHECK(MCGameFolder::Remember(user, other).empty());
    CHECK(MCGameFolder::Remembered(user) == other);
}

/// <summary>
/// The first start copies what the original wrote (Windows' VirtualStore copy of the game folder) into the user folder,
/// never over the user folder's own files; later starts copy nothing, so a save deleted in Redux stays deleted. Off
/// Windows there's no VirtualStore.
/// </summary>
TEST_CASE("launcher: the first start carries over the original's settings and saves")
{
    const ScratchFolder scratch("carry-over");
    const std::filesystem::path store = scratch.Path() / "store";
    const std::filesystem::path user = scratch.Path() / "user";
    const std::filesystem::path saves = std::filesystem::path("data") / "savegame";
    std::filesystem::create_directories(store / saves);
    std::filesystem::create_directories(user / saves);
    std::ofstream(store / "prefs.cfg") << "the original's settings";
    std::ofstream(store / saves / "mine.sav") << "the original's save";
    std::ofstream(store / saves / "both.sav") << "the original's other save";
    std::ofstream(user / saves / "both.sav") << "Redux's save";

    CHECK(MCGameFolder::CarryOver(store, user).empty());
    CHECK_EQ(ReadAll(user / "prefs.cfg"), std::string("the original's settings"));
    CHECK_EQ(ReadAll(user / saves / "mine.sav"), std::string("the original's save"));
    CHECK_EQ(ReadAll(user / saves / "both.sav"), std::string("Redux's save"));

    // A later start, the user folder without a PREFS.CFG of its own even.
    std::filesystem::remove(user / saves / "mine.sav");
    std::filesystem::remove(user / "prefs.cfg");
    CHECK(MCGameFolder::CarryOver(store, user).empty());
    CHECK(!std::filesystem::exists(user / saves / "mine.sav"));
    CHECK(!std::filesystem::exists(user / "prefs.cfg"));

    const std::filesystem::path game = scratch.Path() / "game";
    std::filesystem::create_directories(game);
    CHECK(MCGameFolder::VirtualStore(game).empty());

#if defined(_WIN32)
    // On Windows, a folder on the system drive (the temp folder's) whose VirtualStore copy exists has it.
    const std::filesystem::path virtualStore = MCFileSystem::FromUtf8(SDL_getenv("LOCALAPPDATA")) / "VirtualStore";
    const std::filesystem::path copy = virtualStore / game.relative_path();
    // The outermost folder the test makes there, removed after.
    std::filesystem::path made = copy;

    while (made.parent_path() != virtualStore && !std::filesystem::exists(made.parent_path()))
    {
        made = made.parent_path();
    }

    std::filesystem::create_directories(copy);
    CHECK(MCGameFolder::VirtualStore(game) == copy);
    std::filesystem::remove_all(made);
#endif
}

/// <summary>Copies are named by their Wine wrapper, their CrossOver bottle, or else their folder.</summary>
TEST_CASE("launcher: copies of the game are told apart by wrapper, bottle or folder")
{
    const std::filesystem::path install = std::filesystem::path("Program Files") / "MicroProse" / "MCX";
    CHECK_EQ(MCGameFolder::Label(std::filesystem::path("Applications") / "MechCommander Gold.app" / "Contents" /
                                 "Resources" / "drive_c" / install),
             std::string("MechCommander Gold.app"));
    CHECK_EQ(MCGameFolder::Label(std::filesystem::path("Library") / "Application Support" / "CrossOver" / "Bottles" /
                                 "MCG" / "drive_c" / install),
             std::string("MCG bottle"));
    CHECK_EQ(MCGameFolder::Label(std::filesystem::path("Games") / "MechCommander Gold"),
             std::string("MechCommander Gold"));
}
