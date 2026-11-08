#pragma once

// Port: a WebSocket to the internet games' service (SPEC-in-game-lobby.md's "The service"), polled. Each platform's own
// (the Mac's NSURLSession, Windows' WinHTTP: apps/MCRedux's MCWebSocketMac.mm and MCWebSocketWin.cpp), so the
// service's certificate is checked against the system's own roots and the system's proxy settings apply. (libdatachannel's
// WebSocket checks no certificate on Windows, and on the Mac only against roots the game would ship.)

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// How <see cref="MCWebSocket::WhyClosed"/> starts when the service's certificate was refused (expired, for another
/// name, or from no root the system trusts), on every platform.
/// </summary>
inline constexpr std::string_view MCWebSocketUntrusted = "the service's certificate wasn't trusted";

/// <summary>
/// The close code <see cref="MCWebSocket::CloseAway"/> sends: the application's own (4000 to 4999), which the service
/// takes as a drop, as any but 1000 and 1001.
/// </summary>
inline constexpr uint16_t MCWebSocketAwayCode = 4100;

/// <summary>One WebSocket, opened by <see cref="MCWebSocketOpener"/>: sends queue until it opens. Thread-safe.</summary>
class MCWebSocket
{
public:
    enum class State
    {
        Opening,
        Open,
        /// <summary>Closed by either end, or it never opened: <see cref="WhyClosed"/> says why.</summary>
        Closed,
    };

    /// <summary>A message, whole: text (UTF-8) or binary.</summary>
    struct Message
    {
        bool Binary = false;
        std::string Data;
    };

    virtual ~MCWebSocket() = default;

    virtual void SendText(std::string_view text) = 0;
    virtual void SendBinary(std::span<const uint8_t> bytes) = 0;
    /// <summary>The messages that arrived since the last call, in order.</summary>
    virtual std::vector<Message> Take() = 0;
    virtual State GetState() const = 0;
    /// <summary>Why it closed (the close's code and reason, or the system's error), for the log: never an address.</summary>
    virtual std::string WhyClosed() const = 0;
    /// <summary>The bytes sent and not yet gone to the network, so a sender can wait (the transport's window).</summary>
    virtual size_t Unsent() const = 0;
    /// <summary>
    /// Closes it. What's still waiting to go may not: the service tells the other players this one left, which ends
    /// their links to it.
    /// </summary>
    virtual void Close() = 0;
    /// <summary>
    /// Closes it as a drop, not a leaving (<see cref="MCWebSocketAwayCode"/>): the service keeps its player's place for
    /// a socket to come back to it (a game restarting into an update). Close's otherwise.
    /// </summary>
    virtual void CloseAway() = 0;
};

/// <summary>Opens a WebSocket to <c>wss://</c> <paramref name="url"/> (this platform's, or a test's).</summary>
using MCWebSocketOpener = std::function<std::unique_ptr<MCWebSocket>(const std::string& url)>;
