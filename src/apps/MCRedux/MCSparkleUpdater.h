#pragma once

#include "main/MCUpdater.h"

// Port-only (the personal build's Mac app): the updater's backend (main/MCUpdater.h) on Sparkle 2
// (src/lib/Sparkle-2.10.1). Sparkle reads the feed the app's Info.plist names (SUFeedURL), checks each update's EdDSA
// signature against its SUPublicEDKey, downloads, unpacks and swaps the app; a user driver of ours instead of Sparkle's
// windows reports each step to the updater, whose UPDATE page shows it in the game, and passes on the player's answers.

/// <summary>The Sparkle backend, or none when the app's Info.plist names no feed or Sparkle can't start.</summary>
std::unique_ptr<MCUpdater::MCBackend> MCMakeSparkleBackend();
