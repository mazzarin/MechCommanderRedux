#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

/// <summary>
/// Finding the player's MechCommander Gold when MCRedux isn't started from it: the folder chosen last time (kept in
/// the user folder), the places a copy is usually kept, and otherwise the player's choice from a folder dialog. On
/// macOS the usual places are beside the app, the folders in /Applications and ~/Applications, the Windows drives of
/// Wine wrappers there (Wineskin, Porting Kit) and CrossOver's bottles; elsewhere there are none yet.
/// </summary>
namespace MCGameFolder
{
    /// <summary>Whether <paramref name="folder"/> holds the game's data (its SYSTEM.CFG).</summary>
    bool HasGameData(const std::filesystem::path& folder);

    /// <summary>
    /// Whether <paramref name="folder"/> is a MechCommander Gold install: SYSTEM.CFG beside MCX.EXE. Stricter than
    /// <see cref="HasGameData"/>, for folders the player didn't name (other games have a SYSTEM.CFG too).
    /// </summary>
    bool IsInstall(const std::filesystem::path& folder);

    /// <summary>
    /// The installs in <paramref name="root"/> and the folders under it, at most <paramref name="depth"/> levels down,
    /// sorted. An install's own subfolders (mods) aren't searched. An app bundle is searched only as a Wine wrapper
    /// (its Windows drive), and Windows' own folders, user profiles and symbolic links (a Wine profile links the Mac
    /// user's Desktop and Documents) never: reading those would ask the player for access.
    /// </summary>
    std::vector<std::filesystem::path> FindInstalls(const std::filesystem::path& root, int depth);

    /// <summary>The installs in the usual places (see the namespace), nearest first, each once.</summary>
    std::vector<std::filesystem::path> FindInUsualPlaces();

    /// <summary>The folder chosen last time, or empty when none is remembered in <paramref name="userFolder"/>.</summary>
    std::filesystem::path Remembered(const std::filesystem::path& userFolder);

    /// <summary>Remembers <paramref name="folder"/> in <paramref name="userFolder"/> for the next start.</summary>
    /// <returns>Empty, or what went wrong.</returns>
    std::string Remember(const std::filesystem::path& userFolder, const std::filesystem::path& folder);

    /// <summary>
    /// A short name for <paramref name="install"/> to tell copies apart: its Wine wrapper's app name ("MechCommander
    /// Gold.app"), else its CrossOver bottle's ("MCG bottle"), else its folder's.
    /// </summary>
    std::string Label(const std::filesystem::path& install);

    /// <summary>Whether the player asks to choose the game again (holding Option on macOS as the app opens).</summary>
    bool ChoiceRequested();

    /// <summary>
    /// Asks the player which copy to play: one of <paramref name="found"/>, or another folder from a folder dialog
    /// (searched like <see cref="FindInstalls"/> when it isn't an install itself). Needs SDL's video subsystem.
    /// </summary>
    /// <returns>The chosen folder, empty when the player quits, or why they couldn't be asked.</returns>
    std::expected<std::filesystem::path, std::string> Ask(std::span<const std::filesystem::path> found);
}
