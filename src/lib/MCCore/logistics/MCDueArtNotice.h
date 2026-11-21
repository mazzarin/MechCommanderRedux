#pragma once

// Port-only (Redux HD): the notice that the game shows the old HD art while the new art a program installed alone came
// for downloads or waits for its restart (main/MCUpdater.h's DueArt; SPEC-install-and-updates.md's Forced art), once a
// run, as a mission or a match starts: in single player on the mission's briefing; for a match in its room (the session
// screen), as the player hosts or joins, before the match. Its OK, Enter or Escape closes it. Nothing of it goes over the
// network, and it closes by itself as the room moves on to the match (or the player leaves), so it never holds a
// match's start; the game's own messages come first, and it shows once nothing else is said.

/// <summary>
/// Shows the notice when it's due and its screen is free, and closes it once its screen is left (each logistics frame).
/// </summary>
void ShowDueArtNotice();
