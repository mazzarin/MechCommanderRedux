#pragma once

#include "lib/MCOptionalRef.h"
#include "platform/MCSocket.h"

/// <summary>
/// A network inside the process: TCP connections and UDP datagrams between the sockets of this transport, with no
/// operating system sockets. Every socket is on <see cref="MCSocket::LoopbackIp"/>; a datagram to
/// <see cref="MCSocket::BroadcastIp"/> reaches every UDP socket on its port. Nothing is lost or reordered, and data
/// arrives as soon as it is sent. Thread-safe.
/// </summary>
class MCLoopbackTransport final : public MCNetTransport
{
public:
    /// <summary>The first port handed to a socket opened on port 0 (or a connecting one).</summary>
    static constexpr uint16_t FirstEphemeralPort = 50000;

    /// <summary>Sockets open now.</summary>
    size_t OpenSockets() const;

    /// <summary>Sockets open now that were asked to be kept alive (<see cref="MCSocket::KeepAlive"/>).</summary>
    size_t KeptAliveSockets() const;

    bool Startup() override;
    std::optional<uint32_t> Resolve(std::string_view host) override;
    MCSocket::Handle ListenTcp(uint16_t port) override;
    MCSocket::Handle AcceptTcp(MCSocket::Handle listener, MCSocket::Address& from) override;
    MCSocket::Handle ConnectTcp(const MCSocket::Address& to, int timeoutMs) override;
    void KeepAlive(MCSocket::Handle socket) override;
    int Send(MCSocket::Handle socket, std::span<const uint8_t> data) override;
    int Receive(MCSocket::Handle socket, std::span<uint8_t> data) override;
    MCSocket::Handle OpenUdp(uint16_t port) override;
    bool SendTo(MCSocket::Handle socket, const MCSocket::Address& to, std::span<const uint8_t> data) override;
    int ReceiveFrom(MCSocket::Handle socket, std::span<uint8_t> data, MCSocket::Address& from) override;
    uint16_t LocalPort(MCSocket::Handle socket) override;
    void Close(MCSocket::Handle socket) override;

private:
    /// <summary>What a socket is.</summary>
    enum class Kind
    {
        Listener,
        Stream,
        Udp,
    };

    /// <summary>A datagram waiting to be read.</summary>
    struct Datagram
    {
        MCSocket::Address From;
        std::vector<uint8_t> Bytes;
    };

    /// <summary>One open socket.</summary>
    struct Socket
    {
        Kind Type = Kind::Udp;
        uint16_t Port = 0;
        /// <summary>A stream: the other end (gone when it closed).</summary>
        MCSocket::Handle Peer = MCSocket::InvalidHandle;
        /// <summary>A stream: the other end's port, for AcceptTcp.</summary>
        uint16_t PeerPort = 0;
        /// <summary>A stream: bytes arrived and not yet read.</summary>
        std::deque<uint8_t> Inbox;
        /// <summary>A listener: connections not yet accepted.</summary>
        std::deque<MCSocket::Handle> Pending;
        /// <summary>A UDP socket: datagrams not yet read.</summary>
        std::deque<Datagram> Datagrams;
        /// <summary>Whether it was asked to be kept alive (nothing here ever goes away unannounced).</summary>
        bool KeptAlive = false;
    };

    /// <summary>Opens a socket of <paramref name="type"/> on <paramref name="port"/> (0 = a free one).</summary>
    MCSocket::Handle Open(Kind type, uint16_t port);

    /// <summary>Whether a socket of <paramref name="type"/> is on <paramref name="port"/>.</summary>
    bool PortTaken(Kind type, uint16_t port) const;

    /// <summary>The socket <paramref name="handle"/> names, if any.</summary>
    MCOptionalRef<Socket> Find(MCSocket::Handle handle);

    mutable std::mutex _Lock;
    std::map<MCSocket::Handle, Socket> _Sockets;
    MCSocket::Handle _NextHandle = 1;
    uint16_t _NextPort = FirstEphemeralPort;
};
