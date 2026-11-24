#pragma once

// The load and save screens' callbacks: campaign saves (.sav), single missions (.sol) and the host's multiplayer
// missions. Original source: logistics\logmain.cpp.

#include "gui/MCScreenChange.h"

#include <string_view>

class MCGuiEvent;
class MCGuiObject;

/// <summary>Set while the load screen lists single missions (.sol) rather than campaign saves.</summary>
extern bool LoadingSolo;

/// <summary>What the load screen says when the game refuses the save chosen (the original said nothing).</summary>
inline constexpr std::string_view RefusedSaveText = "THIS SAVE COULDN'T BE LOADED.";

/// <summary>Opens the save game screen (not before a campaign has a mission).</summary>
void SaveScreen();

/// <summary>Opens the load game screen.</summary>
void LoadScreen();

/// <summary>Opens the load screen for single missions.</summary>
void SoloLoadScreen();

/// <summary>
/// Loads the save game (or single mission) chosen on the load screen; one the game refuses leaves the load screen up,
/// saying so.
/// </summary>
void LoadGame();

/// <summary>
/// The save <paramref name="name"/> loaded (a campaign's .sav, or a single mission's .sol), as the load screen's LOAD
/// loads it: the music stopped, the briefing set up on success; whether the game took it (a refused one leaves no
/// campaign).
/// </summary>
bool LoadSavedGame(std::string_view name, bool campaign);

/// <summary>
/// Port-only (SPEC-menus.md's changes of screen): a save's load (<see cref="LoadSavedGame"/>'s) as a change's job, run
/// live from its press a step a frame; <see cref="ShowSavedGame"/> ends it once the screen is dark.
/// </summary>
struct MCSavedGameLoad
{
    std::vector<MCScreenChange::MCPiece> Job;
    /// <summary>The load's result once its job has run: 0 loaded, else refused.</summary>
    std::shared_ptr<int32_t> Result;
};

/// <summary>The load of save <paramref name="name"/> (a campaign's .sav, or a single mission's .sol), begun.</summary>
MCSavedGameLoad LoadSavedGameAhead(std::string_view name, bool campaign);

/// <summary>
/// A save's load ended, its job run: the music stopped; on success (<paramref name="result"/> 0) the briefing set up and
/// the logistics music on. Whether the game took the save (a refused one leaves no campaign).
/// </summary>
bool ShowSavedGame(int32_t result);

/// <summary>Loads the multiplayer mission chosen on the load screen (the host's session screen).</summary>
void LoadMPGame();

/// <summary>The "game saved" dialog's answer: back to the logistics screen left.</summary>
void SaveWorkedCallback(int32_t result);

/// <summary>Saves the game under the selected entry's name.</summary>
void SaveGameCallback();

/// <summary>After confirming an overwrite: deletes the old file and saves.</summary>
void ClearForSaveGameCallback();

/// <summary>The save button: names the entry (the name typed, or the first free default), asks before overwriting, saves.</summary>
void SaveGame();

/// <summary>Confirmed: deletes the chosen file.</summary>
void DeleteCallbackTrue();

/// <summary>Not confirmed: keeps the file (and takes down the name entry).</summary>
void DeleteCallbackFalse();

/// <summary>The delete button: asks for confirmation.</summary>
void DeleteGame();

/// <summary>The load and save screens' routine: the file pane's selection and Enter in the name entry.</summary>
void LoadSaveScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event);
