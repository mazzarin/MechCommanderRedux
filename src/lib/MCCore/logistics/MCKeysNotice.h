#pragma once

// Port-only: the question asked once about each command whose new default key the player uses for another (as LIGHTS'
// N came after players could bind N). Port-only (SPEC-menus.md's stage 9, step 2): pulled out of the 1998 CONTROLS panel
// (logistics/MCControlsPanel), as the reports' and the old HD art's notices are their own.

/// <summary>
/// Port-only: once the main menu, or a lobby's session screen, is free, asks the player for a key for each command whose
/// new default key they use for another (<see cref="MCKeyBindings::TakeGivenWay"/>, as LIGHTS' N came after players could
/// bind N): the next key not in use is its key, OK leaves it without one; PREFS keeps either, so it's asked once.
/// </summary>
void AskGivenWayKeys();
