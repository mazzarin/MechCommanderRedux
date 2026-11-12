// Built without the precompiled header (see MCCore.vcxproj and MCSocket.h): the system socket headers must not meet
// the game's Win32 stand-ins.

#include "platform/MCSocket.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <mutex>

namespace
{
#ifdef _WIN32
    using NativeSocket = SOCKET;
    constexpr NativeSocket NativeInvalid = INVALID_SOCKET;

    /// <summary>Whether the last call failed only because it would have blocked.</summary>
    bool WouldBlock()
    {
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
    }

    void CloseNative(NativeSocket socket)
    {
        closesocket(socket);
    }

    bool SetNonBlocking(NativeSocket socket)
    {
        u_long enable = 1;
        return ioctlsocket(socket, FIONBIO, &enable) == 0;
    }
#else
    using NativeSocket = int;
    constexpr NativeSocket NativeInvalid = -1;

    bool WouldBlock()
    {
        return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
    }

    void CloseNative(NativeSocket socket)
    {
        close(socket);
    }

    bool SetNonBlocking(NativeSocket socket)
    {
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
    }
#endif

    NativeSocket ToNative(MCSocket::Handle handle)
    {
        return static_cast<NativeSocket>(handle);
    }

    MCSocket::Handle FromNative(NativeSocket socket)
    {
        return socket == NativeInvalid ? MCSocket::InvalidHandle : static_cast<MCSocket::Handle>(socket);
    }

    sockaddr_in ToSockaddr(const MCSocket::Address& address)
    {
        sockaddr_in result{};
        result.sin_family = AF_INET;
        result.sin_addr.s_addr = htonl(address.Ip);
        result.sin_port = htons(address.Port);
        return result;
    }

    MCSocket::Address FromSockaddr(const sockaddr_in& address)
    {
        return MCSocket::Address{ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)};
    }

    /// <summary>The TCP option for the quiet seconds before keep-alive's first question (macOS's own name).</summary>
#ifdef TCP_KEEPIDLE
    constexpr int KeepIdleOption = TCP_KEEPIDLE;
#else
    constexpr int KeepIdleOption = TCP_KEEPALIVE;
#endif

    /// <summary>
    /// The TCP option for the seconds sent data may go unacknowledged (macOS's and Windows'; -1 where there is none,
    /// and the system's own retransmission limit stands).
    /// </summary>
#if defined(TCP_RXT_CONNDROPTIME)
    constexpr int DropOption = TCP_RXT_CONNDROPTIME;
#elif defined(TCP_MAXRT)
    constexpr int DropOption = TCP_MAXRT;
#else
    constexpr int DropOption = -1;
#endif

    /// <summary>
    /// Sets the whole-number option <paramref name="name"/> of <paramref name="level"/>. A system without it keeps its
    /// own value: the socket works as before.
    /// </summary>
    void SetOption(NativeSocket socket, int level, int name, int value)
    {
        setsockopt(socket, level, name, reinterpret_cast<const char*>(&value), sizeof(value));
    }

    /// <summary>The whole-number option <paramref name="name"/> of <paramref name="level"/>, if reported.</summary>
    std::optional<int> OptionOf(NativeSocket socket, int level, int name)
    {
        int value = 0;
        socklen_t size = sizeof(value);

        if (getsockopt(socket, level, name, reinterpret_cast<char*>(&value), &size) != 0)
        {
            return std::nullopt;
        }

        return value;
    }
}

namespace MCSocket
{
    std::string ToString(const Address& address)
    {
        return std::to_string((address.Ip >> 24) & 0xff) + "." + std::to_string((address.Ip >> 16) & 0xff) + "." +
               std::to_string((address.Ip >> 8) & 0xff) + "." + std::to_string(address.Ip & 0xff) + ":" +
               std::to_string(address.Port);
    }
}

bool MCSocketTransport::Startup()
{
#ifdef _WIN32
    static std::once_flag once;
    static bool started = false;
    std::call_once(once,
                   []
                   {
                       WSADATA data{};
                       started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
                   });
    return started;
#else
    return true;
#endif
}

std::optional<uint32_t> MCSocketTransport::Resolve(std::string_view host)
{
    if (host.empty() || !Startup())
    {
        return std::nullopt;
    }

    const std::string name(host);
    in_addr numeric{};

    if (inet_pton(AF_INET, name.c_str(), &numeric) == 1)
    {
        return ntohl(numeric.s_addr);
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* results = nullptr;

    if (getaddrinfo(name.c_str(), nullptr, &hints, &results) != 0 || results == nullptr)
    {
        return std::nullopt;
    }

    const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(results->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(results);
    return ip;
}

MCSocket::Handle MCSocketTransport::ListenTcp(uint16_t port)
{
    if (!Startup())
    {
        return MCSocket::InvalidHandle;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (socket == NativeInvalid)
    {
        return MCSocket::InvalidHandle;
    }

    const sockaddr_in local = ToSockaddr(MCSocket::Address{0, port});

#ifndef _WIN32
    // Port (personal): a host starting again on its port (a restart into an update, network/MCLobbyUpdate.h) takes it
    // while its last run's links wait out TIME_WAIT, which would refuse the bind for half a minute. (Windows binds
    // then anyway, and its SO_REUSEADDR would let another program take the port.)
    const int reuse = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    if (bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0 || listen(socket, 8) != 0 ||
        !SetNonBlocking(socket))
    {
        CloseNative(socket);
        return MCSocket::InvalidHandle;
    }

    return FromNative(socket);
}

MCSocket::Handle MCSocketTransport::AcceptTcp(MCSocket::Handle listener, MCSocket::Address& from)
{
    sockaddr_in remote{};
    socklen_t remoteSize = sizeof(remote);
    const NativeSocket socket = accept(ToNative(listener), reinterpret_cast<sockaddr*>(&remote), &remoteSize);

    if (socket == NativeInvalid)
    {
        return MCSocket::InvalidHandle;
    }

    if (!SetNonBlocking(socket))
    {
        CloseNative(socket);
        return MCSocket::InvalidHandle;
    }

    const int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    from = FromSockaddr(remote);
    return FromNative(socket);
}

MCSocket::Handle MCSocketTransport::ConnectTcp(const MCSocket::Address& to, int timeoutMs)
{
    if (!Startup())
    {
        return MCSocket::InvalidHandle;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (socket == NativeInvalid)
    {
        return MCSocket::InvalidHandle;
    }

    if (!SetNonBlocking(socket))
    {
        CloseNative(socket);
        return MCSocket::InvalidHandle;
    }

    const int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    const sockaddr_in remote = ToSockaddr(to);

    if (connect(socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0)
    {
        if (!WouldBlock())
        {
            CloseNative(socket);
            return MCSocket::InvalidHandle;
        }

#ifdef _WIN32
        fd_set writable;
        fd_set failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(socket, &writable);
        FD_SET(socket, &failed);
        timeval wait{timeoutMs / 1000, (timeoutMs % 1000) * 1000};

        if (select(0, nullptr, &writable, &failed, &wait) <= 0 || FD_ISSET(socket, &failed))
        {
            CloseNative(socket);
            return MCSocket::InvalidHandle;
        }
#else
        pollfd poller{socket, POLLOUT, 0};

        if (poll(&poller, 1, timeoutMs) <= 0)
        {
            CloseNative(socket);
            return MCSocket::InvalidHandle;
        }
#endif

        int error = 0;
        socklen_t errorSize = sizeof(error);
        getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &errorSize);

        if (error != 0)
        {
            CloseNative(socket);
            return MCSocket::InvalidHandle;
        }
    }

    return FromNative(socket);
}

void MCSocketTransport::KeepAlive(MCSocket::Handle socket)
{
    // The three keep-alive times need Windows 10's 2017 updates; an older system asks after its own idle time (hours).
    const NativeSocket native = ToNative(socket);
    constexpr MCSocket::KeepAliveTimes times = MCSocket::LinkKeepAlive;
    SetOption(native, SOL_SOCKET, SO_KEEPALIVE, 1);
    SetOption(native, IPPROTO_TCP, KeepIdleOption, times.IdleSeconds);
    SetOption(native, IPPROTO_TCP, TCP_KEEPINTVL, times.IntervalSeconds);
    SetOption(native, IPPROTO_TCP, TCP_KEEPCNT, times.Probes);

    if constexpr (DropOption >= 0)
    {
        SetOption(native, IPPROTO_TCP, DropOption, times.DropSeconds);
    }
}

std::optional<MCSocket::KeepAliveTimes> MCSocketTransport::KeepAliveOf(MCSocket::Handle socket)
{
    const NativeSocket native = ToNative(socket);

    if (OptionOf(native, SOL_SOCKET, SO_KEEPALIVE).value_or(0) == 0)
    {
        return std::nullopt;
    }

    const auto tcp = [&](int name) { return name >= 0 ? OptionOf(native, IPPROTO_TCP, name).value_or(0) : 0; };
    return MCSocket::KeepAliveTimes{tcp(KeepIdleOption), tcp(TCP_KEEPINTVL), tcp(TCP_KEEPCNT), tcp(DropOption)};
}

int MCSocketTransport::Send(MCSocket::Handle socket, std::span<const uint8_t> data)
{
#ifdef _WIN32
    const int sent =
        send(ToNative(socket), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
#else
    const int sent = static_cast<int>(send(ToNative(socket), data.data(), data.size(), MSG_NOSIGNAL));
#endif

    if (sent < 0)
    {
        return WouldBlock() ? 0 : -1;
    }

    return sent;
}

int MCSocketTransport::Receive(MCSocket::Handle socket, std::span<uint8_t> data)
{
#ifdef _WIN32
    const int received = recv(ToNative(socket), reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()), 0);
#else
    const int received = static_cast<int>(recv(ToNative(socket), data.data(), data.size(), 0));
#endif

    if (received == 0)
    {
        return -1;
    }

    if (received < 0)
    {
        return WouldBlock() ? 0 : -1;
    }

    return received;
}

MCSocket::Handle MCSocketTransport::OpenUdp(uint16_t port)
{
    if (!Startup())
    {
        return MCSocket::InvalidHandle;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (socket == NativeInvalid)
    {
        return MCSocket::InvalidHandle;
    }

    const int enable = 1;
    setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enable), sizeof(enable));
    const sockaddr_in local = ToSockaddr(MCSocket::Address{0, port});

    if (bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0 || !SetNonBlocking(socket))
    {
        CloseNative(socket);
        return MCSocket::InvalidHandle;
    }

    return FromNative(socket);
}

bool MCSocketTransport::SendTo(MCSocket::Handle socket, const MCSocket::Address& to, std::span<const uint8_t> data)
{
    const sockaddr_in remote = ToSockaddr(to);
#ifdef _WIN32
    return sendto(ToNative(socket), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                  reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) >= 0;
#else
    return sendto(ToNative(socket), data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&remote),
                  sizeof(remote)) >= 0;
#endif
}

int MCSocketTransport::ReceiveFrom(MCSocket::Handle socket, std::span<uint8_t> data, MCSocket::Address& from)
{
    sockaddr_in remote{};
    socklen_t remoteSize = sizeof(remote);
#ifdef _WIN32
    const int received = recvfrom(ToNative(socket), reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()),
                                  0, reinterpret_cast<sockaddr*>(&remote), &remoteSize);

    if (received < 0)
    {
        // A datagram larger than the buffer arrives cut short with WSAEMSGSIZE; an ICMP "port unreachable" from
        // an earlier send shows up as WSAECONNRESET. Neither is a reason to stop reading.
        const int error = WSAGetLastError();

        if (error == WSAEMSGSIZE)
        {
            return static_cast<int>(data.size());
        }

        return (WouldBlock() || error == WSAECONNRESET) ? 0 : -1;
    }
#else
    const int received = static_cast<int>(
        recvfrom(ToNative(socket), data.data(), data.size(), 0, reinterpret_cast<sockaddr*>(&remote), &remoteSize));

    if (received < 0)
    {
        return WouldBlock() ? 0 : -1;
    }
#endif

    from = FromSockaddr(remote);
    return received;
}

uint16_t MCSocketTransport::LocalPort(MCSocket::Handle socket)
{
    sockaddr_in local{};
    socklen_t localSize = sizeof(local);

    if (getsockname(ToNative(socket), reinterpret_cast<sockaddr*>(&local), &localSize) != 0)
    {
        return 0;
    }

    return ntohs(local.sin_port);
}

void MCSocketTransport::Close(MCSocket::Handle socket)
{
    if (socket != MCSocket::InvalidHandle)
    {
        CloseNative(ToNative(socket));
    }
}

bool MCSocketTransport::EndSending(MCSocket::Handle socket)
{
    if (socket == MCSocket::InvalidHandle)
    {
        return false;
    }

#ifdef _WIN32
    return shutdown(ToNative(socket), SD_SEND) == 0;
#else
    return shutdown(ToNative(socket), SHUT_WR) == 0;
#endif
}
