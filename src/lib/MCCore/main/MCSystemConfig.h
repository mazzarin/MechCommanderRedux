#pragma once

#include <string>
#include <string_view>

// The game's settings at start-up (honorb.cpp, the "HonorBound" codename): SYSTEM.CFG (sound, ABL, the data paths) and
// PREFS.CFG (display, gamma, language, difficulty, volumes).

class MCFitIniFile;

/// <summary>PREFS.CFG's name, in the player's settings folder (MCFileSystem::SettingsToRead, SettingsToWrite).</summary>
inline constexpr std::string_view PrefsFile = "prefs.cfg";

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
/// What PREFS.CFG's "MechCommander" settings read as when the file lacks them (the original's, and the port's own).
/// Port: <see cref="SavePrefs"/> writes a setting only when it differs from its default (these, the HD settings' own,
/// AUTO for the scroll speed and the interface size, the effects' volume for the interface sounds', VULKAN for the
/// renderer), so a missing key follows the build: a release that changes a default reaches every player who never
/// picked otherwise.
/// </summary>
namespace MCPrefsDefaults
{
    inline constexpr bool PaletteCycle = false;
    /// <summary>"Gamma", and "Brightness", which wins.</summary>
    inline constexpr int32_t Gamma = 0;
    inline constexpr bool Force32Mb = false;
    inline constexpr bool Force16Mb = false;
    /// <summary>"DirectDraw": full screen.</summary>
    inline constexpr bool FullScreen = false;
    inline constexpr bool StretchToFit = false;
    inline constexpr bool SoftwareCursor = false;
    inline constexpr bool ShowFps = false;
    /// <summary>"UIResolution": native.</summary>
    inline constexpr int32_t UiResolution = 0;
    inline constexpr int32_t Difficulty = 1;
    inline constexpr int32_t MusicVolume = 0x40;
    inline constexpr int32_t RadioVolume = 0x40;
    inline constexpr int32_t SfxVolume = 0x60;
}

/// <summary>
/// Port: PREFS.CFG's "PrefsVersion", the format <see cref="SavePrefs"/> writes. Missing: written by a build that wrote
/// every setting, defaults too. 1: only the settings that differ from their defaults. A release that must change a
/// setting even where the player chose it does so once, for files older than its own version, and writes its version.
/// </summary>
inline constexpr int32_t PrefsVersion = 1;

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
/// resolution), the scroll speed, language, difficulty and volumes. A missing entry takes its default
/// (<see cref="MCPrefsDefaults"/>); a missing block is fatal. Port:
/// then the key bindings the player changed, from the "Controls" block (<see cref="MCKeyBindings::ReadPrefs"/>).
/// </summary>
void ReadPreferences(MCFitIniFile& file);
