#pragma once

// Port: the game's network (SPEC-in-game-lobby.md's "In the game"): the system's sockets, or an internet game's links
// (platform/MCIceTransport) while one is on. Each socket is held to the network it was opened on, so a socket opened
// before an internet game began (the LAN search's) closes on the system's, and one of the game's stops working once
// the game is over, whatever the order things close in: the two number their sockets alike, and a socket closed on the
// wrong one would close another.

#include "lib/MCOptionalRef.h"
#include "platform/MCSocket.h"

#include <map>
#include <memory>
#include <mutex>

/// <summary>The system's sockets, or an internet game's network while one is on (see the file's comment). Thread-safe.</summary>
class MCNetSwitch final : public MCNetTransport
{
public:
    explicit MCNetSwitch(std::unique_ptr<MCNetTransport> system);
    MCNetSwitch(const MCNetSwitch&) = delete;
    MCNetSwitch& operator=(const MCNetSwitch&) = delete;

    /// <summary>The switch <paramref name="net"/> is, if it is one (a test's network isn't).</summary>
    static MCOptionalRef<MCNetSwitch> Of(MCNetTransport& net);

    /// <summary>
    /// Sockets opened from now on (and names resolved) are on <paramref name="internet"/>'s network until
    /// <see cref="EndInternet"/>; those open now stay on the system's.
    /// </summary>
    void BeginInternet(std::shared_ptr<MCNetTransport> internet);
    /// <summary>
    /// Back to the system's sockets: the internet game's sockets read as closed from now, and the switch lets its
    /// network go (it closes its links as it's destroyed).
    /// </summary>
    void EndInternet();
    /// <summary>Ends <paramref name="internet"/>'s network, as <see cref="EndInternet()"/>, if it's the one on.</summary>
    void EndInternet(const MCNetTransport& internet);
    /// <summary>Whether an internet game's network is on.</summary>
    bool InternetOn() const;

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

private:
    /// <summary>A socket of the switch's: the network it's on and its handle there.</summary>
    struct Held
    {
        /// <summary>Opened on the internet game's network (the one on now: a game's sockets go as it ends).</summary>
        bool Internet = false;
        MCSocket::Handle Inner = MCSocket::InvalidHandle;
    };

    /// <summary>
    /// A network and a socket's handle on it, the internet game's kept while it's used (its game may end meanwhile).
    /// </summary>
    struct Route
    {
        std::shared_ptr<MCNetTransport> Internet;
        MCNetTransport& Net;
        MCSocket::Handle Inner = MCSocket::InvalidHandle;
    };

    /// <summary>Ends the internet game's network on now, if any (and if it's <paramref name="only"/>, when given).</summary>
    void EndIf(MCOptionalRef<const MCNetTransport> only = std::nullopt);
    /// <summary>The network new sockets go on now (its handle unset).</summary>
    Route Current();
    /// <summary>The network <paramref name="socket"/> is on and its handle there; none for one that's gone.</summary>
    std::optional<Route> Find(MCSocket::Handle socket);
    /// <summary>The network <paramref name="held"/> is on (under the lock).</summary>
    Route RouteOf(const Held& held) const;
    /// <summary>
    /// This switch's handle for <paramref name="inner"/>, opened on <paramref name="route"/>'s network; none when
    /// it failed, or when it's an internet game's that ended meanwhile (the socket is closed there).
    /// </summary>
    MCSocket::Handle Keep(const Route& route, MCSocket::Handle inner);

    std::unique_ptr<MCNetTransport> _System;
    mutable std::mutex _Lock;
    std::shared_ptr<MCNetTransport> _Internet;
    std::map<MCSocket::Handle, Held> _Held;
    MCSocket::Handle _NextHandle = 1;
};
