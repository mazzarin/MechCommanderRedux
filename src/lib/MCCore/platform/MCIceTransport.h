#pragma once

// Port: the internet games' network (SPEC-in-game-lobby.md's "In the game"): MCNetTransport over ICE links
// (platform/MCIceLink), so MCDirectPlay and everything above it play an internet game as they play a LAN one.
//
// - A game is reached by its name on the service (its code): Resolve gives it an address of its own in 0.0.0.0/8, a
//   block no machine is ever reached at, and a datagram or connection to that address goes over the one link this game
//   keeps to that game, opened by the first of them. The host's side gives each guest's link such an address too, so
//   its answers and accepted connections name it.
// - A link carries frames: datagrams (each port to port, whole), and the streams a guest connects (TCP's ports, bytes
//   in order, a close). A host's link takes connections only to a port listened on; a datagram to a port nothing is
//   open on is lost, as UDP's.
// - The service only passes each link's signals while it opens (MCIceSignaling); once open, the link is the two games'.
//
// Everything is polled: each call first moves the service's signals and the links' bytes along. Thread-safe.

#include "lib/MCOptionalRef.h"
#include "platform/MCIceLink.h"
#include "platform/MCSocket.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// The service's part in an internet game's links: it passes each link's signals between the game that hosts and a game
/// that reached it, while the link opens (the game's own object on the service). Polled by MCIceTransport.
/// </summary>
class MCIceSignaling
{
public:
    /// <summary>Who sent what was heard.</summary>
    enum class From
    {
        /// <summary>A game that reached this one, as its host (Peer: the guest, as the service names it).</summary>
        Guest,
        /// <summary>The host of a game this one reached (Peer: the game's name).</summary>
        Host,
    };

    /// <summary>One thing heard, in the order its sender said it.</summary>
    struct Heard
    {
        From Sender = From::Guest;
        std::string Peer;
        /// <summary>Which of the guest's links it is about (the guest numbers them).</summary>
        uint32_t Link = 0;
        /// <summary>None: the peer left the service (a guest gone), or no game has that name.</summary>
        std::optional<MCIceLink::Signal> Signal;
    };

    virtual ~MCIceSignaling() = default;

    /// <summary>What a new link starts with: the service's STUN and relays, the relays' short-lived credentials.</summary>
    virtual MCIceLink::Settings LinkSettings() = 0;

    /// <summary>To the host of the game <paramref name="game"/>, about this game's link <paramref name="link"/> to it.</summary>
    virtual void ToHost(std::string_view game, uint32_t link, const MCIceLink::Signal& signal) = 0;

    /// <summary>To the guest <paramref name="guest"/>, about its link <paramref name="link"/>.</summary>
    virtual void ToGuest(std::string_view guest, uint32_t link, const MCIceLink::Signal& signal) = 0;

    /// <summary>What was heard since the last call.</summary>
    virtual std::vector<Heard> Take() = 0;
};

/// <summary>The network of an internet game: MCNetTransport over ICE links (see the file's comment).</summary>
class MCIceTransport final : public MCNetTransport
{
public:
    /// <summary>How long links wait (the tests' are shorter).</summary>
    struct Deadlines
    {
        /// <summary>For a link to open (ICE trying every path, the relay's last) before it fails.</summary>
        std::chrono::milliseconds Open{15000};
        /// <summary>
        /// For a guest's link to carry a connection (it asked about the game, or its connection ended): longer, and the
        /// host closes it, so idle links never keep players out. A game that asks again opens another.
        /// </summary>
        std::chrono::milliseconds Idle{30000};
    };

    explicit MCIceTransport(std::shared_ptr<MCIceSignaling> signaling);
    MCIceTransport(std::shared_ptr<MCIceSignaling> signaling, Deadlines deadlines);
    ~MCIceTransport() override;
    MCIceTransport(const MCIceTransport&) = delete;
    MCIceTransport& operator=(const MCIceTransport&) = delete;

    /// <summary>Links open or opening now (the tests').</summary>
    size_t Links() const;

    bool Startup() override;
    /// <summary>The address of the game named <paramref name="host"/> on the service (the same each time).</summary>
    std::optional<uint32_t> Resolve(std::string_view host) override;
    MCSocket::Handle ListenTcp(uint16_t port) override;
    MCSocket::Handle AcceptTcp(MCSocket::Handle listener, MCSocket::Address& from) override;
    /// <summary>
    /// Connects to a game's address (Resolve's), waiting up to <paramref name="timeoutMs"/> for its link to open, if it
    /// isn't, and for its host to take the connection.
    /// </summary>
    MCSocket::Handle ConnectTcp(const MCSocket::Address& to, int timeoutMs) override;
    /// <summary>Nothing to ask: ICE checks the path every few seconds and fails the link once the other end is gone.</summary>
    void KeepAlive(MCSocket::Handle socket) override;
    int Send(MCSocket::Handle socket, std::span<const uint8_t> data) override;
    int Receive(MCSocket::Handle socket, std::span<uint8_t> data) override;
    MCSocket::Handle OpenUdp(uint16_t port) override;
    /// <summary>To a game's address or a guest link's; anything else (a broadcast, a machine) goes nowhere.</summary>
    bool SendTo(MCSocket::Handle socket, const MCSocket::Address& to, std::span<const uint8_t> data) override;
    int ReceiveFrom(MCSocket::Handle socket, std::span<uint8_t> data, MCSocket::Address& from) override;
    uint16_t LocalPort(MCSocket::Handle socket) override;
    void Close(MCSocket::Handle socket) override;

private:
    enum class Kind
    {
        Listener,
        Stream,
        Udp,
    };

    enum class StreamState
    {
        /// <summary>Connecting: waiting for the host to take it.</summary>
        Asked,
        Open,
        /// <summary>No listener took it.</summary>
        Refused,
        /// <summary>The other end closed it: what arrived is read, then it reads as closed.</summary>
        Ended,
        /// <summary>Its link failed or closed.</summary>
        Broken,
    };

    struct Datagram
    {
        MCSocket::Address From;
        std::vector<uint8_t> Bytes;
    };

    struct Socket
    {
        Kind Type = Kind::Udp;
        uint16_t Port = 0;
        /// <summary>A stream: its link (this transport's number), its number on it, the other end's port.</summary>
        uint32_t Link = 0;
        uint32_t Stream = 0;
        uint16_t PeerPort = 0;
        StreamState State = StreamState::Asked;
        /// <summary>A guest's stream: its host took it (it may have closed since).</summary>
        bool Accepted = false;
        std::deque<uint8_t> Inbox;
        /// <summary>A listener: connections taken and not yet accepted.</summary>
        std::deque<MCSocket::Handle> Pending;
        /// <summary>A UDP socket: datagrams not yet read.</summary>
        std::deque<Datagram> Datagrams;
    };

    struct Link
    {
        std::unique_ptr<MCIceLink> Ice;
        /// <summary>This game reached the other (its guest end), or answered it (its host end).</summary>
        bool Guest = false;
        /// <summary>The game's name (a guest end's), or the guest's as the service names it (a host end's).</summary>
        std::string Peer;
        /// <summary>The link's number in its signals (the guest's).</summary>
        uint32_t Number = 0;
        /// <summary>The other end's address here (the game's, or the guest link's).</summary>
        uint32_t Ip = 0;
        std::chrono::steady_clock::time_point Started;
        bool WasOpen = false;
        /// <summary>Since when it has carried no connection (a host end's: <see cref="Deadlines::Idle"/>).</summary>
        std::chrono::steady_clock::time_point IdleSince;
        /// <summary>Since when nothing has used it: it closes once what was sent has gone, or after a while.</summary>
        std::optional<std::chrono::steady_clock::time_point> UnusedSince;
        /// <summary>Frames waiting for the link to open.</summary>
        std::vector<uint8_t> Early;
        /// <summary>Bytes received short of a whole frame.</summary>
        std::vector<uint8_t> Input;
        /// <summary>A guest end: the UDP sockets that sent over it (with its streams, what keeps it).</summary>
        std::set<MCSocket::Handle> Senders;
        uint32_t NextStream = 1;
    };

    /// <summary>Moves the service's signals and the links' bytes along (under the lock).</summary>
    void Pump();
    void Hear(const MCIceSignaling::Heard& heard);
    /// <summary>Reads the frames <paramref name="link"/> received; false when one can't be taken (it can't be trusted).</summary>
    bool ReadFrames(uint32_t id, Link& link);
    /// <summary>Handles one frame; false when it can't be (the link can't be trusted).</summary>
    bool HandleFrame(uint32_t id, Link& link, uint8_t kind, std::span<const uint8_t> body);
    /// <summary>Sends a frame now, or once the link opens.</summary>
    static void SendFrame(Link& link, uint8_t kind, std::span<const uint8_t> head, std::span<const uint8_t> bytes = {});
    /// <summary>Ends a link: its streams break, and it goes.</summary>
    void EndLink(uint32_t id, std::string_view why);
    /// <summary>Closes the links nothing keeps any more (under the lock).</summary>
    void DropUnused();
    /// <summary>This game's link to the game at <paramref name="ip"/>, opened if it has none.</summary>
    std::optional<uint32_t> LinkToGame(uint32_t ip);
    /// <summary>The host end of the guest link at <paramref name="ip"/>.</summary>
    std::optional<uint32_t> LinkAt(uint32_t ip) const;
    /// <summary>The stream that is <paramref name="stream"/> on the link <paramref name="link"/>.</summary>
    MCOptionalRef<Socket> StreamOn(uint32_t link, uint32_t stream);
    bool Listening() const;
    MCSocket::Handle Open(Kind type, uint16_t port);
    bool PortTaken(Kind type, uint16_t port) const;
    MCOptionalRef<Socket> Find(MCSocket::Handle handle, Kind type);
    void CloseLocked(MCSocket::Handle handle);

    std::shared_ptr<MCIceSignaling> _Signaling;
    Deadlines _Deadlines;
    mutable std::mutex _Lock;
    std::map<MCSocket::Handle, Socket> _Sockets;
    std::map<uint32_t, Link> _Links;
    /// <summary>The games' names by their addresses here, and back.</summary>
    std::map<uint32_t, std::string> _GameNames;
    std::map<std::string, uint32_t, std::less<>> _GameIps;
    MCSocket::Handle _NextHandle = 1;
    uint16_t _NextPort;
    uint32_t _NextLink = 1;
    uint32_t _NextIp;
};
