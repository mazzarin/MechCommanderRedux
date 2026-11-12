#pragma once

// The port's thin wrapper over the operating system's sockets (Winsock on Windows, BSD sockets elsewhere), used by
// MCDirectPlay. Its .cpp is built without the precompiled header (MCCore.vcxproj) so the system headers it needs
// (winsock2.h, which drags in windows.h) never meet the game's own Win32 stand-ins. This header therefore uses only
// standard types.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/// <summary>Non-blocking TCP and UDP sockets over IPv4.</summary>
namespace MCSocket
{
    /// <summary>An open socket; <see cref="InvalidHandle"/> when none.</summary>
    using Handle = std::intptr_t;

    /// <summary>No socket.</summary>
    inline constexpr Handle InvalidHandle = -1;

    /// <summary>An IPv4 address and port, both in host byte order.</summary>
    struct Address
    {
        uint32_t Ip = 0;
        uint16_t Port = 0;

        friend bool operator==(const Address&, const Address&) = default;
    };

    /// <summary>The broadcast address (255.255.255.255).</summary>
    inline constexpr uint32_t BroadcastIp = 0xffffffffu;
    /// <summary>The loopback address (127.0.0.1).</summary>
    inline constexpr uint32_t LoopbackIp = 0x7f000001u;
    /// <summary>The loopback's network (127.0.0.0/8) and its mask.</summary>
    inline constexpr uint32_t LoopbackMask = 0xff000000u;

    /// <summary>Whether <paramref name="ip"/> (host order) is on this computer's loopback (127.0.0.0/8).</summary>
    constexpr bool IsLoopback(uint32_t ip)
    {
        return (ip & LoopbackMask) == (LoopbackIp & LoopbackMask);
    }

    /// <summary>Starts the socket library (once per process; later calls do nothing).</summary>
    /// <returns>Whether sockets are usable.</returns>
    bool Startup();

    /// <summary>Resolves <paramref name="host"/> (a dotted address or a host name) to an IPv4 address.</summary>
    std::optional<uint32_t> Resolve(std::string_view host);

    /// <summary>The address as dotted text ("192.168.0.4:28800").</summary>
    std::string ToString(const Address& address);

    /// <summary>A non-blocking TCP socket listening on <paramref name="port"/> of every interface.</summary>
    Handle ListenTcp(uint16_t port);

    /// <summary>
    /// Accepts a pending connection on <paramref name="listener"/>, made non-blocking; <paramref name="from"/> gets
    /// its address.
    /// </summary>
    /// <returns>The connection, or <see cref="InvalidHandle"/> when none is waiting.</returns>
    Handle AcceptTcp(Handle listener, Address& from);

    /// <summary>
    /// Connects to <paramref name="to"/>, waiting up to <paramref name="timeoutMs"/> milliseconds; the connected
    /// socket is non-blocking.
    /// </summary>
    /// <returns>The connection, or <see cref="InvalidHandle"/> when it failed or timed out.</returns>
    Handle ConnectTcp(const Address& to, int timeoutMs);

    /// <summary>
    /// How soon a connection kept alive (<see cref="KeepAlive"/>) fails once the machine at its other end has gone:
    /// switched off, asleep or off the network. The operating systems ask and answer, never the games, so a game busy
    /// loading or drawing slowly is never taken for gone.
    /// </summary>
    struct KeepAliveTimes
    {
        /// <summary>Seconds without a word from the other end before asking whether it is there.</summary>
        int IdleSeconds = 0;
        /// <summary>Seconds between the questions.</summary>
        int IntervalSeconds = 0;
        /// <summary>Questions left unanswered before the connection fails.</summary>
        int Probes = 0;
        /// <summary>Seconds data sent may go unacknowledged before the connection fails.</summary>
        int DropSeconds = 0;

        friend bool operator==(const KeepAliveTimes&, const KeepAliveTimes&) = default;
    };

    /// <summary>A machine gone is noticed within about half a minute, the connection quiet or busy.</summary>
    inline constexpr KeepAliveTimes LinkKeepAlive{10, 5, 4, 30};

    /// <summary>
    /// Has the operating system watch the TCP connection <paramref name="socket"/> for the machine at its other end
    /// going (<see cref="LinkKeepAlive"/>); the connection then fails as a closed one does. A system without one of
    /// the settings keeps its own for it.
    /// </summary>
    void KeepAlive(Handle socket);

    /// <summary>Sends what the connection takes of <paramref name="data"/> without blocking.</summary>
    /// <returns>The bytes sent (0 when the connection is busy), or -1 when the connection failed.</returns>
    int Send(Handle socket, std::span<const uint8_t> data);

    /// <summary>Reads what has arrived on a connection into <paramref name="data"/>, without blocking.</summary>
    /// <returns>The bytes read (0 when nothing is waiting), or -1 when the connection closed or failed.</returns>
    int Receive(Handle socket, std::span<uint8_t> data);

    /// <summary>
    /// A non-blocking UDP socket bound to <paramref name="port"/> (0 = any free port), allowed to broadcast.
    /// </summary>
    Handle OpenUdp(uint16_t port);

    /// <summary>Sends one datagram.</summary>
    bool SendTo(Handle socket, const Address& to, std::span<const uint8_t> data);

    /// <summary>
    /// Reads one waiting datagram (cut to the size of <paramref name="data"/>) without blocking; <paramref name="from"/>
    /// gets the sender.
    /// </summary>
    /// <returns>Its size, 0 when none is waiting, or -1 on error.</returns>
    int ReceiveFrom(Handle socket, std::span<uint8_t> data, Address& from);

    /// <summary>The local port <paramref name="socket"/> is bound to.</summary>
    uint16_t LocalPort(Handle socket);

    /// <summary>Closes <paramref name="socket"/> (nothing for <see cref="InvalidHandle"/>).</summary>
    void Close(Handle socket);

    /// <summary>
    /// Port-only (Redux HD): ends what this end sends on the connection <paramref name="socket"/> (a TCP half-close):
    /// the other end reads all of it and then that the connection closed, while what it sends can still be read here.
    /// Close it once the other end has closed its end, or after a while: a socket closed with bytes unread resets the
    /// connection, and Windows then throws away what the other end hadn't read.
    /// </summary>
    /// <returns>Whether the network half-closes connections; when it doesn't, nothing is done (close it).</returns>
    bool EndSending(Handle socket);
}

/// <summary>
/// The network, as the port service MCDirectPlay talks through: the <c>MCSocket</c> calls (which forward to the
/// current context's transport), each meaning what that function's summary says.
/// </summary>
class MCNetTransport
{
public:
    virtual ~MCNetTransport() = default;

    /// <summary>See <see cref="MCSocket::Startup"/>.</summary>
    virtual bool Startup() = 0;
    /// <summary>See <see cref="MCSocket::Resolve"/>.</summary>
    virtual std::optional<uint32_t> Resolve(std::string_view host) = 0;
    /// <summary>See <see cref="MCSocket::ListenTcp"/>.</summary>
    virtual MCSocket::Handle ListenTcp(uint16_t port) = 0;
    /// <summary>See <see cref="MCSocket::AcceptTcp"/>.</summary>
    virtual MCSocket::Handle AcceptTcp(MCSocket::Handle listener, MCSocket::Address& from) = 0;
    /// <summary>See <see cref="MCSocket::ConnectTcp"/>.</summary>
    virtual MCSocket::Handle ConnectTcp(const MCSocket::Address& to, int timeoutMs) = 0;
    /// <summary>See <see cref="MCSocket::KeepAlive"/>.</summary>
    virtual void KeepAlive(MCSocket::Handle socket) = 0;
    /// <summary>See <see cref="MCSocket::Send"/>.</summary>
    virtual int Send(MCSocket::Handle socket, std::span<const uint8_t> data) = 0;
    /// <summary>See <see cref="MCSocket::Receive"/>.</summary>
    virtual int Receive(MCSocket::Handle socket, std::span<uint8_t> data) = 0;
    /// <summary>See <see cref="MCSocket::OpenUdp"/>.</summary>
    virtual MCSocket::Handle OpenUdp(uint16_t port) = 0;
    /// <summary>See <see cref="MCSocket::SendTo"/>.</summary>
    virtual bool SendTo(MCSocket::Handle socket, const MCSocket::Address& to, std::span<const uint8_t> data) = 0;
    /// <summary>See <see cref="MCSocket::ReceiveFrom"/>.</summary>
    virtual int ReceiveFrom(MCSocket::Handle socket, std::span<uint8_t> data, MCSocket::Address& from) = 0;
    /// <summary>See <see cref="MCSocket::LocalPort"/>.</summary>
    virtual uint16_t LocalPort(MCSocket::Handle socket) = 0;
    /// <summary>See <see cref="MCSocket::Close"/>.</summary>
    virtual void Close(MCSocket::Handle socket) = 0;
    /// <summary>See <see cref="MCSocket::EndSending"/> (a network that doesn't half-close: false).</summary>
    virtual bool EndSending(MCSocket::Handle) { return false; }
};

/// <summary>The operating system's sockets (Winsock on Windows, BSD sockets elsewhere).</summary>
class MCSocketTransport final : public MCNetTransport
{
public:
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
    bool EndSending(MCSocket::Handle socket) override;

    /// <summary>
    /// The times the operating system watches the TCP connection <paramref name="socket"/> by, as it reports them;
    /// nothing when it doesn't watch it (as <see cref="KeepAlive"/> left it).
    /// </summary>
    static std::optional<MCSocket::KeepAliveTimes> KeepAliveOf(MCSocket::Handle socket);
};
