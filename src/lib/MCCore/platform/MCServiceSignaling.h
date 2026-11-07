#pragma once

// Port: the service's part in an internet game's links (MCIceSignaling) over its WebSockets (platform/MCWebSocket): a
// host's socket to /host makes its game and gets the game's code; a guest's to /join/<code> joins it. Each game's
// object on the service passes the links' signals, and the bytes of a link it carries (the WebSocket path). The
// protocol is the trial service's (tools/cloudflare/trial/src/game.js in the workspace); the service the INTERNET
// screen uses (step 3) adds the list, names and counts. Every message from the service is checked before it's used.

#include "platform/MCIceTransport.h"
#include "platform/MCWebSocket.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// <summary>The service's signals and a link's bytes, over its WebSockets. Thread-safe.</summary>
class MCServiceSignaling final : public MCIceSignaling
{
public:
    /// <summary>
    /// The service at <paramref name="service"/> (<c>wss://host</c>), its sockets opened by <paramref name="open"/>. A
    /// public game's links take the relays' path only (<paramref name="relayOnly"/>: MCIceLink::Settings::RelayOnly).
    /// </summary>
    MCServiceSignaling(std::string service, MCWebSocketOpener open, bool relayOnly);
    ~MCServiceSignaling() override;
    MCServiceSignaling(const MCServiceSignaling&) = delete;
    MCServiceSignaling& operator=(const MCServiceSignaling&) = delete;

    /// <summary>Hosts a game on the service: its code comes with the service's answer (<see cref="HostedCode"/>).</summary>
    void Host();
    /// <summary>The code of the game this one hosts, once the service gave it.</summary>
    std::optional<std::string> HostedCode();
    /// <summary>Whether this game's socket to its host's game is in (the service sent the relays for its links).</summary>
    bool Joined(std::string_view game);
    /// <summary>Opens this game's socket to the game <paramref name="game"/> (the first signal to it would too).</summary>
    void Join(std::string_view game);
    /// <summary>Why the host's socket or <paramref name="game"/>'s closed, if it did (for the log and the trial).</summary>
    std::optional<std::string> WhyClosed(std::string_view game = {});

    MCIceLink::Settings LinkSettings() override;
    void ToHost(std::string_view game, uint32_t link, const MCIceLink::Signal& signal) override;
    void ToGuest(std::string_view guest, uint32_t link, const MCIceLink::Signal& signal) override;
    void BytesToHost(std::string_view game, uint32_t link, std::span<const uint8_t> bytes) override;
    void BytesToGuest(std::string_view guest, uint32_t link, std::span<const uint8_t> bytes) override;
    std::vector<Heard> Take() override;

private:
    /// <summary>One socket: the host's to its game, or a guest's to a game it reached.</summary>
    struct Socket
    {
        std::unique_ptr<MCWebSocket> Ws;
        /// <summary>The service's hosted or joined came: links have their relays.</summary>
        bool In = false;
        /// <summary>The closing was passed on (as the peer gone), so it's said once.</summary>
        bool Told = false;
        std::chrono::steady_clock::time_point LastPing;
    };

    Socket& GameSocket(std::string_view game);
    /// <summary>A socket to <paramref name="url"/>, or one closed at once when the system can't open it.</summary>
    std::unique_ptr<MCWebSocket> Opened(const std::string& url);
    void Ping(Socket& socket, std::chrono::steady_clock::time_point now);
    void HearHost(const MCWebSocket::Message& message, std::vector<Heard>& heard);
    void HearGame(std::string_view game, Socket& socket, const MCWebSocket::Message& message,
                  std::vector<Heard>& heard);

    std::string _Service;
    MCWebSocketOpener _Open;
    std::mutex _Lock;
    MCIceLink::Settings _Settings;
    std::optional<Socket> _Host;
    std::optional<std::string> _HostedCode;
    std::map<std::string, Socket, std::less<>> _Games;
};
