#pragma once

#include <expected>
#include <filesystem>
#include <string>

/// <summary>
/// Port-only (Redux HD): MechCommander Online's fingerprint of a player's game data (a Redux room's engine_content,
/// which every player's must match to join), as its launcher works it out (4.0.6.8's "room-core-v2"): SHA-256 over
/// "mco-redux-room-core-v2\n" and, for each file sorted by name, its name, size and SHA-256 on lines of their own. The
/// files: the game folder's .fst .pak .fit .abl .dpk .csv .map (any case), named by their path with '/' in upper case;
/// none under a folder named Redux, Temp or Tmp, nor in Redux HD's own folders (Redux update, Redux backup, HD); not
/// WINDOWS.FIT, .mpk or .pkk, nor under DATA/SAVEGAME/, DATA/MISSIONS/ or DATA/TERRAIN/; and an .fst holding only maps'
/// files (DATA/MISSIONS/, DATA/TERRAIN/) left out unless it is ART, MISC, MISSION, SHAPES or TERRAIN.FST. The user
/// folder overlays the game's data (MCFileSystem): its files of those kinds under DATA/OBJECTS/, and its .fst anywhere,
/// by the same rules but for the Redux, Temp and Tmp folders, stand in for the game folder's of the same name. A link
/// is never followed. Names are upper-cased as ASCII (the retail game's are).
/// </summary>
namespace MCMcoContent
{
    /// <summary>
    /// The fingerprint of the data in <paramref name="gameFolder"/> overlaid by <paramref name="userFolder"/> (none: the
    /// game folder's alone), 64 lower-case hex digits, or why not.
    /// </summary>
    std::expected<std::string, std::string> Fingerprint(const std::filesystem::path& gameFolder,
                                                        const std::filesystem::path& userFolder = {});
}
