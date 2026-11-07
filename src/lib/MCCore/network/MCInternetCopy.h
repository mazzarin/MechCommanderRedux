#pragma once

// Port (Redux HD): this copy's ID for the internet games' service (SPEC-in-game-lobby.md's "Identity"), which every
// game's socket says (MCServiceIdentity) and the service's bans name. Made the first time it's asked for and kept in
// the user folder's internet.cfg; apart from the reports' ID (reports.cfg), which goes when REPORTS does.

#include <string>

/// <summary>This copy's ID, made and kept if it has none (a run that can't keep it has one for itself).</summary>
std::string MCInternetCopyId();
