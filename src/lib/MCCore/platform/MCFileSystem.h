#pragma once

#include "platform/MCServices.h"
#include "platform/MCUtf8Path.h"

/// <summary>
/// Where the game's files are, and how its paths map onto the disk. The original ran from its install folder and
/// opened files by relative DOS paths (<c>data\art\ACCESS00.tga</c>, any case). The port runs from anywhere: every
/// relative path the game uses resolves against <see cref="GameRoot"/>, the retail install (<c>--data</c>), with
/// <c>\</c> as a separator and case ignored, so the same paths work on a case-sensitive file system.
///
/// The install is never written to. When a user folder is set (<see cref="SetUserRoot"/>), it overlays the install:
/// a relative path is looked up there first, and everything the game creates, renames or deletes happens there
/// (saves, PREFS.CFG, the temp FITs). The one exception is deleting a save (<see cref="RemoveSave"/>), which also
/// removes the install's copy. Without one (the tests), both roots are the install.
///
/// Every function forwards to the current context's <see cref="MCFileSource"/> (platform/MCServices.h): the disk
/// (<see cref="MCDiskFileSource"/>) in the game, maybe files in memory in a test.
///
/// Game paths are UTF-8 strings (platform/MCUtf8Path.h converts them).
/// </summary>
namespace MCFileSystem
{
    /// <summary>Case-insensitive DOS wildcard match (<c>*</c> and <c>?</c>) of a whole name.</summary>
    bool WildcardMatch(std::string_view pattern, std::string_view name);

    /// <summary>
    /// Whether <paramref name="a"/> comes before <paramref name="b"/> in the order the game lists files and searches
    /// FastFiles: by the names with their ASCII letters upper-cased, then by their bytes; the same on every system and
    /// in every locale. It is NTFS's own order for ASCII names (<c>_</c> after the letters): Windows lists as before.
    /// </summary>
    bool NameLess(std::string_view a, std::string_view b);

    /// <summary>Sets the folder of the retail install (the one holding MCX.EXE's data: ART.FST, DATA\, ...).</summary>
    void SetGameRoot(const std::filesystem::path& root);

    /// <summary>The folder of the retail install.</summary>
    const std::filesystem::path& GameRoot();

    /// <summary>Sets the writable folder that overlays the install (created if missing).</summary>
    void SetUserRoot(const std::filesystem::path& root);

    /// <summary>The writable folder: the user folder when set, else the install.</summary>
    const std::filesystem::path& UserRoot();

    /// <summary>
    /// Port-only: sets the folder that keeps the player's own settings (PREFS.CFG, the reports' state) when it isn't the
    /// user folder, as when a launcher gives a match a user folder of its own (created if missing; empty: the user
    /// folder keeps them, as it keeps everything else). A folder the game's paths can't reach (a network share), or
    /// that can't be made, isn't set: the user folder keeps them.
    /// </summary>
    /// <returns>Whether the folder is set.</returns>
    bool SetSettingsRoot(const std::filesystem::path& root);

    /// <summary>
    /// The path to read the settings file <paramref name="name"/> from: the settings folder's copy, when one is set and
    /// holds it (an absolute path), else <paramref name="name"/> (the user folder's, else the install's).
    /// </summary>
    std::string SettingsToRead(std::string_view name);

    /// <summary>
    /// The path to save the settings file <paramref name="name"/> to: in the settings folder when one is set (an
    /// absolute path), else <paramref name="name"/> (the user folder's).
    /// </summary>
    std::string SettingsToWrite(std::string_view name);

    /// <summary>
    /// Maps a path as the game writes it to the file on disk, for reading. A relative path is taken from the user
    /// folder when it exists there, else from the game root; each component is matched without regard to case
    /// against what exists. Components that don't exist yet (a file about to be created) are kept as written.
    /// </summary>
    /// <param name="gamePath">The path as the original code spells it.</param>
    std::filesystem::path Resolve(std::string_view gamePath);

    /// <summary>
    /// Maps a path to where the game may write it: under the user folder for a relative path (its folders are
    /// created). When <paramref name="copyExisting"/> is set and the file exists only in the install, it is copied
    /// over first, so opening it for update changes the copy.
    /// </summary>
    std::filesystem::path ResolveWrite(std::string_view gamePath, bool copyExisting = false);

    /// <summary>
    /// Whether <paramref name="gamePath"/>, a path from outside the game (another machine's message), names something
    /// in the game's folders: not empty, relative, and with no ".." part, drive or stream (no ':').
    /// </summary>
    bool IsContainedPath(std::string_view gamePath);

    /// <summary>Creates a folder (the CRT's <c>_mkdir</c>), under the user folder for a relative path.</summary>
    /// <returns>Whether the folder exists afterwards.</returns>
    bool MakeDirectory(std::string_view gamePath);

    /// <summary>Deletes an empty folder (the CRT's <c>_rmdir</c>). Folders in the install are left alone.</summary>
    /// <returns>Whether the folder was deleted.</returns>
    bool RemoveDirectory(std::string_view gamePath);

    /// <summary>
    /// The files in a folder whose names match a DOS wildcard (<c>*</c>, <c>?</c>; case ignored), as the CRT's
    /// <c>_findfirst</c>/<c>_findnext</c> list them: names only, user folder and install merged. Port: in name order
    /// (<see cref="NameLess"/>), whatever order the system lists the folder in (APFS's and NTFS's differ, and neither
    /// promises one).
    /// </summary>
    std::vector<std::string> FindFiles(std::string_view gamePattern);

    /// <summary>
    /// The same files as <see cref="FindFiles"/>, each with the folders that have it: the user folder, the install or
    /// both. Without a user folder, every file is the install's.
    /// </summary>
    std::vector<MCFoundFile> FindFileSources(std::string_view gamePattern);

    /// <summary>
    /// The contents of <paramref name="gamePath"/> when the file source holds it in memory (a test's
    /// <c>MCMemoryFileSource</c>); the disk holds none.
    /// </summary>
    std::optional<std::span<const uint8_t>> FindImage(std::string_view gamePath);

    /// <summary>What <see cref="PutImage"/>'s maker gives: the file's bytes, or none (the file reads as before).</summary>
    using MCImageMaker = std::function<std::optional<std::vector<uint8_t>>()>;

    /// <summary>
    /// Port-only: from now on the game reads <paramref name="gamePath"/> (any case, either slash) as the bytes
    /// <paramref name="make"/> gives the first time it's opened to read (MCFile::Open), whatever the folders and
    /// FastFiles hold: a file the game makes for itself (the benchmark's battle mission, mission/MCTrailerMission.h).
    /// A file opened to write is still the folders'.
    /// </summary>
    void PutImage(std::string_view gamePath, MCImageMaker make);

    /// <summary>The bytes <see cref="PutImage"/> put at <paramref name="gamePath"/> (made now if not yet), or none.</summary>
    std::optional<std::vector<uint8_t>> PutImageAt(std::string_view gamePath);

    /// <summary>Whether <see cref="PutImage"/> put <paramref name="gamePath"/> (not making it), from any thread.</summary>
    bool PutsImage(std::string_view gamePath);

    /// <summary>Takes away every file <see cref="PutImage"/> put.</summary>
    void ClearPutImages();

    /// <summary>Whether <paramref name="gamePath"/> names an existing loose file (not one inside a FastFile).</summary>
    bool Exists(std::string_view gamePath);

    /// <summary>Deletes a loose file (the CRT's <c>remove</c>). Files in the install are left alone.</summary>
    /// <returns>Whether a file was deleted.</returns>
    bool RemoveFile(std::string_view gamePath);

    /// <summary>
    /// Deletes a save game (<c>.sav</c>, <c>.sol</c>) wherever it is: the user folder's copy and the install's. Saves
    /// are the player's files, not the game's; the original kept them in its own folder, and the install may still
    /// hold some (the retail game ships a few).
    /// </summary>
    /// <returns>Whether no copy is left.</returns>
    bool RemoveSave(std::string_view gamePath);

    /// <summary>
    /// Renames a loose file (the CRT's <c>rename</c>). Unlike the CRT's, an existing target is replaced; the original
    /// removed the target first anyway.
    /// </summary>
    /// <returns>Whether the file was renamed.</returns>
    bool RenameFile(std::string_view fromGamePath, std::string_view toGamePath);

    /// <summary>
    /// Copies a loose file over another (Win32 <c>CopyFile</c> with overwrite, as the original's editors used to keep
    /// a <c>.bak</c>).
    /// </summary>
    /// <returns>Whether the copy was made.</returns>
    bool CopyFile(std::string_view fromGamePath, std::string_view toGamePath);
}
