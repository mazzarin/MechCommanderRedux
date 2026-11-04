#pragma once

// Port: the gameplay data a match plays by, hashed, so the host can tell a player whose mission or game data differ
// from its own. The original's file report sent the mission file's first four bytes (GetCheckSum), which told little
// more than that the file was there, so players with different data played on and drifted apart.
//
// The files are those play reads, found as the game finds them (MCFile::Open: a loose file, the user folder's before
// the install's, else the first archive holding it), chosen by rule so that a file added later is in:
// - Every match's (worked out once a run and kept, MCGameContext::SharedDataHash): every file in the objects folder
//   (data\objects\: the objects' definitions, the components), the pilots' (data\missions\warriors\: pilots and their
//   brains) and the units' profiles (data\missions\profiles\); the game system (data\missions\gamesys.fit) and the ABL
//   libraries (data\missions\*.abx); the mechs' shape data their types read (data\sprites\*.hsp, *.inf, *.out, *.jmp:
//   where their weapons fire from, how high they jump), not their pictures.
// - The mission's: its file (data\savegame\<name>.mpk), the purchase list it names, its scenario
//   (data\missions\<scenario>.fit), the scenario's script, and every file named after its map in data\terrain\ but the
//   map's pictures (.tga, .gif).
// - With every script among them (.abl, .abx, .abi), the files it includes (#include, #include_), as ABL opens them.
// Editors' backups (.bak) are left out. The art, sounds, movies, fonts, palettes, the HD pack and the player's own
// settings never count: they change what a player sees and hears, not what the game does.

/// <summary>The game paths of every match's gameplay files, as the game finds them now, by name (case ignored).</summary>
std::vector<std::string> MCSharedDataFiles();

/// <summary>
/// The game paths of mission file <paramref name="missionFile"/>'s own gameplay files (itself first among them), by
/// name (case ignored); those its files name that the game hasn't are in too, as they count missing.
/// </summary>
std::vector<std::string> MCMissionDataFiles(std::string_view missionFile);

/// <summary>
/// The hash of every match's gameplay data (XXH3-64), worked out the first time in a context and then kept (the files
/// don't change while the game runs).
/// </summary>
uint64_t MCSharedDataHash();

/// <summary>
/// What a file report says of mission file <paramref name="missionFile"/> (a game path to a .mpk): 0 when the game
/// hasn't it, as the original's report; else the hash of its own and every match's gameplay data, folded to 32 bits,
/// never 0.
/// </summary>
uint32_t MCMissionDataHash(std::string_view missionFile);
