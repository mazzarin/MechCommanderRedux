#pragma once

// The game's settings at start-up (honorb.cpp, the "HonorBound" codename): SYSTEM.CFG (sound, ABL, the data paths) and
// PREFS.CFG (display, gamma, language, difficulty, volumes).

class MCFitIniFile;

/// <summary>The campaign control FIT the game session starts ("campaign", in the mission path).</summary>
extern std::string CampaignFile;
/// <summary>SYSTEM.CFG "missionName": the mission FIT a game segment (-mission N) starts.</summary>
extern std::string MissionName;
/// <summary>SYSTEM.CFG ABL "DebuggerEnabled": the game session makes the ABL debugger window.</summary>
extern bool AblDebuggerEnabled;
/// <summary>SYSTEM.CFG has a "DebugGameSystem" block: the game session makes the game system's text window.</summary>
extern bool DebugGameSystem;
/// <summary>
/// Port-only: switch sound and music off whatever SYSTEM.CFG says (the tests that run a mission: playback runs on real
/// time, and the radio and music roll the game's dice when a sound ends, so with sound two runs part).
/// </summary>
extern bool GNoSound;

/// <summary>
/// Reads SYSTEM.CFG and opens every FastFile, then reads PREFS.CFG. A SYSTEM.CFG that can't be opened ends the game.
/// </summary>
void SystemInit();

/// <summary>
/// Reads SYSTEM.CFG's settings from <paramref name="file"/>: whether sound and music play (empty "UseSound" and
/// "UseMusic" blocks switch them on), "DebugGameSystem", the ABL block, and the data paths (making this process's
/// temporary folder under the save path). A missing ABL entry or path is fatal.
/// </summary>
void ReadSystemConfig(MCFitIniFile& file);

/// <summary>
/// Reads PREFS.CFG's "MechCommander" block from <paramref name="file"/>: palette cycling, gamma (which "Brightness"
/// overrides), sprite sizes, the memory switches, the display (full screen, stretch, cursor, frame counter, renderer,
/// resolution), language, difficulty and volumes. A missing entry takes its default; a missing block is fatal. Port:
/// then the key bindings the player changed, from the "Controls" block (<see cref="MCKeyBindings::ReadPrefs"/>).
/// </summary>
void ReadPreferences(MCFitIniFile& file);
