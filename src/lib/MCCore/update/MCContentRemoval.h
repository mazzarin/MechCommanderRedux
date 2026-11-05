#pragma once

class MCUpdateDisk;

/// <summary>
/// Port-only: removing the game's content from a game folder (SPEC-install-and-updates.md's Uninstalling), which the
/// Windows uninstaller has the game do (<c>MCRedux --remove-content</c>) before it puts back what the setup replaced
/// and removes the program: the installer never owns content, so the game is its one remover. Under the game's lock,
/// taken exclusive so no copy plays meanwhile, a swap a crash stopped midway is finished first; then each file the
/// installed record says updates put there goes, when its hash is still the recorded one; then the update folder
/// (<c>Redux update/</c>, the record, the sets chosen and the locks with it); then each folder those files left empty.
/// Files the player added or changed stay, with their folders, and so do the files a copy taken over already had as
/// the release has them (MCInstalled::MCFile::Found), so that copy is as it was.
/// </summary>
namespace MCContentRemoval
{
    /// <summary>How long removal waits for running copies of the game to quit.</summary>
    constexpr std::chrono::seconds GameWait{15};

    /// <summary>What removal did with the recorded files.</summary>
    struct MCRemoved
    {
        /// <summary>The files removed.</summary>
        size_t Removed = 0;
        /// <summary>The recorded files left: the player's (changed, or theirs), and those found in a copy taken over.</summary>
        size_t Kept = 0;
    };

    /// <summary>
    /// Removes the content of <paramref name="gameFolder"/>, waiting up to <paramref name="wait"/> for running copies
    /// of the game to quit. A folder with no update folder has none to remove.
    /// </summary>
    /// <returns>What it removed and left, or why it couldn't (a copy still running, a record it can't read, a file it
    /// can't remove: what it could remove is gone).</returns>
    std::expected<MCRemoved, std::string> Remove(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                 std::chrono::milliseconds wait = GameWait);
}
