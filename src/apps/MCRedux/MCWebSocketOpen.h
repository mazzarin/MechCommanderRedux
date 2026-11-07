#pragma once

#include "platform/MCWebSocket.h"

#include <memory>
#include <string>

// Port-only (the personal build): this platform's WebSocket to the internet games' service (platform/MCWebSocket.h):
// the Mac's NSURLSession (MCWebSocketMac.mm) or Windows' WinHTTP (MCWebSocketWin.cpp), as the updates' downloads use.

/// <summary>Opens a WebSocket to <paramref name="url"/> (<c>wss://</c>, or <c>ws://</c> for a local test service).</summary>
std::unique_ptr<MCWebSocket> MCOpenWebSocket(const std::string& url);
