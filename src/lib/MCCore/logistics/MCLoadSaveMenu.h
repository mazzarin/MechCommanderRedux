#pragma once

// The load and save screens' callbacks: campaign saves (.sav), single missions (.sol) and the host's multiplayer
// missions. Original source: logistics\logmain.cpp.

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
