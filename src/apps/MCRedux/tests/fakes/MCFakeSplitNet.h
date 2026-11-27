#pragma once

#include "platform/MCSocket.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>

// Port-only (Redux HD): two networks in one process for the tests of a game hosted on both (SPEC-mco-hosting.md): an
// internet game's (platform/MCIceTransport) and this computer's sockets, which an MCO guest's game plays on as
// upstream's does. New sockets (and names resolved) go on the one the test picks; each socket stays on the network it
// was opened on, an accepted one on its listener's. Unlike MCNetSwitch, the test can go back and forth, both kept open.
class MCFakeSplitNet final : public MCNetTransport
{
public:
    MCFakeSplitNet(std::unique_ptr<MCNetTransport> system, std::shared_ptr<MCNetTransport> internet)
        : _System(std::move(system)), _Internet(std::move(internet))
    {
    }

    /// <summary>Sockets opened from now on go on the internet game's network (true) or this computer's.</summary>
    void OpenOnInternet(bool internet)
    {
        const std::scoped_lock lock(_Lock);
        _OnInternet = internet;
    }

    bool Startup() override { return _System->Startup() && _Internet->Startup(); }

    std::optional<uint32_t> Resolve(std::string_view host) override { return Current().Resolve(host); }

    MCSocket::Handle ListenTcp(uint16_t port) override
    {
        MCNetTransport& net = Current();
        return Keep(net, net.ListenTcp(port));
    }

    MCSocket::Handle AcceptTcp(MCSocket::Handle listener, MCSocket::Address& from) override
    {
        const std::optional<Held> held = Find(listener);
        return held.has_value() ? Keep(held->Net, held->Net.AcceptTcp(held->Inner, from)) : MCSocket::InvalidHandle;
    }

    MCSocket::Handle ConnectTcp(const MCSocket::Address& to, int timeoutMs) override
    {
        MCNetTransport& net = Current();
        return Keep(net, net.ConnectTcp(to, timeoutMs));
    }

    void KeepAlive(MCSocket::Handle socket) override
    {
        if (const std::optional<Held> held = Find(socket))
        {
            held->Net.KeepAlive(held->Inner);
        }
    }

    int Send(MCSocket::Handle socket, std::span<const uint8_t> data) override
    {
        const std::optional<Held> held = Find(socket);
        return held.has_value() ? held->Net.Send(held->Inner, data) : -1;
    }

    int Receive(MCSocket::Handle socket, std::span<uint8_t> data) override
    {
        const std::optional<Held> held = Find(socket);
        return held.has_value() ? held->Net.Receive(held->Inner, data) : -1;
    }

    MCSocket::Handle OpenUdp(uint16_t port) override
    {
        MCNetTransport& net = Current();
        return Keep(net, net.OpenUdp(port));
    }

    bool SendTo(MCSocket::Handle socket, const MCSocket::Address& to, std::span<const uint8_t> data) override
    {
        const std::optional<Held> held = Find(socket);
        return held.has_value() && held->Net.SendTo(held->Inner, to, data);
    }

    int ReceiveFrom(MCSocket::Handle socket, std::span<uint8_t> data, MCSocket::Address& from) override
    {
        const std::optional<Held> held = Find(socket);
        return held.has_value() ? held->Net.ReceiveFrom(held->Inner, data, from) : -1;
    }

    uint16_t LocalPort(MCSocket::Handle socket) override
    {
        const std::optional<Held> held = Find(socket);
        return held.has_value() ? held->Net.LocalPort(held->Inner) : 0;
    }

    void Close(MCSocket::Handle socket) override
    {
        std::optional<Held> held;
        {
            const std::scoped_lock lock(_Lock);
            const auto found = _Held.find(socket);

            if (found == _Held.end())
            {
                return;
            }

            held.emplace(found->second);
            _Held.erase(found);
        }

        held->Net.Close(held->Inner);
    }

    bool EndSending(MCSocket::Handle socket) override
    {
        const std::optional<Held> held = Find(socket);
        return held.has_value() && held->Net.EndSending(held->Inner);
    }

private:
    /// <summary>A socket of this network's: the network it's on and its handle there.</summary>
    struct Held
    {
        MCNetTransport& Net;
        MCSocket::Handle Inner = MCSocket::InvalidHandle;
    };

    MCNetTransport& Current()
    {
        const std::scoped_lock lock(_Lock);
        return _OnInternet ? *_Internet : *_System;
    }

    std::optional<Held> Find(MCSocket::Handle socket)
    {
        const std::scoped_lock lock(_Lock);
        const auto found = _Held.find(socket);
        return found != _Held.end() ? std::optional<Held>(found->second) : std::nullopt;
    }

    /// <summary>This network's handle for <paramref name="inner"/>, opened on <paramref name="net"/>; none for none.</summary>
    MCSocket::Handle Keep(MCNetTransport& net, MCSocket::Handle inner)
    {
        if (inner == MCSocket::InvalidHandle)
        {
            return MCSocket::InvalidHandle;
        }

        const std::scoped_lock lock(_Lock);
        const MCSocket::Handle handle = _NextHandle++;
        _Held.emplace(handle, Held{net, inner});
        return handle;
    }

    std::unique_ptr<MCNetTransport> _System;
    std::shared_ptr<MCNetTransport> _Internet;
    std::mutex _Lock;
    bool _OnInternet = false;
    std::map<MCSocket::Handle, Held> _Held;
    MCSocket::Handle _NextHandle = 1;
};
