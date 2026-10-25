#include "stdafx.h"
#include "../MCGameFolder.h"
#include "MCTest.h"

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
        CHECK_EQ(found[i].string(), expected[i].string());
    }

    // A wrapper itself, as the player might choose it.
    const std::vector<std::filesystem::path> inWrapper = MCGameFolder::FindInstalls(root / "MechCommander Gold.app", 0);
    REQUIRE_EQ(inWrapper.size(), size_t{1});
    CHECK_EQ(inWrapper.front().string(), wrapped.string());

    CHECK(MCGameFolder::HasGameData(lowercase));
    CHECK(MCGameFolder::HasGameData(root / "No Exe"));
    CHECK(!MCGameFolder::IsInstall(root / "No Exe"));
    CHECK(!MCGameFolder::HasGameData(root / "Missing"));
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
