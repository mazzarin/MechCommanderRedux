#pragma once

// Port-only (Redux HD; iface/MCTouchpadNotice.h): on Windows, the mouse's raw input watched for the empty packets a
// touchpad driver leaves where it dropped a click with a key held (MCTouchpadWatchWin.cpp). The Mac's trackpad drops
// none: there is no watcher there.

/// <summary>
/// Starts the watcher, once, on a thread of its own for the rest of the run: a message-only window registered for the
/// mouse's raw input, which Windows sends it while the game is in front. SDL registers the mouse's raw input only in
/// relative mouse mode, which the game never uses, so the two never take each other's.
/// </summary>
void MCStartTouchpadWatch();
