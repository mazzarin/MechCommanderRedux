#include "stdafx.h"
#include "platform/MCNetSwitch.h"

MCNetSwitch::MCNetSwitch(std::unique_ptr<MCNetTransport> system) : _System(std::move(system))
{
}

MCOptionalRef<MCNetSwitch> MCNetSwitch::Of(MCNetTransport& net)
{
    auto* const found = dynamic_cast<MCNetSwitch*>(&net);
    return found != nullptr ? MCOptionalRef<MCNetSwitch>(*found) : std::nullopt;
}

void MCNetSwitch::BeginInternet(std::shared_ptr<MCNetTransport> internet)
{
    EndInternet();
    const std::scoped_lock lock(_Lock);
    _Internet = std::move(internet);
}

void MCNetSwitch::EndInternet()
{
    EndIf();
}

void MCNetSwitch::EndInternet(const MCNetTransport& internet)
{
    EndIf(internet);
}

void MCNetSwitch::EndIf(MCOptionalRef<const MCNetTransport> only)
{
    std::shared_ptr<MCNetTransport> ended;

    {
        const std::scoped_lock lock(_Lock);

        if (_Internet == nullptr || (only.has_value() && _Internet.get() != &only->get()))
        {
            return;
        }

        ended = std::exchange(_Internet, nullptr);
        std::erase_if(_Held, [](const auto& held) { return held.second.Internet; });
    }

    // Let go outside the lock: its links close as it is destroyed, once no call in flight holds it.
    ended.reset();
}

bool MCNetSwitch::InternetOn() const
{
    const std::scoped_lock lock(_Lock);
    return _Internet != nullptr;
}

MCNetSwitch::Route MCNetSwitch::Current()
{
    const std::scoped_lock lock(_Lock);
    return _Internet != nullptr ? Route{.Internet = _Internet, .Net = *_Internet} : Route{.Net = *_System};
}

std::optional<MCNetSwitch::Route> MCNetSwitch::Find(MCSocket::Handle socket)
{
    const std::scoped_lock lock(_Lock);
    const auto found = _Held.find(socket);

    if (found == _Held.end())
    {
        return std::nullopt;
    }

    return RouteOf(found->second);
}

MCNetSwitch::Route MCNetSwitch::RouteOf(const Held& held) const
{
    return held.Internet ? Route{.Internet = _Internet, .Net = *_Internet, .Inner = held.Inner}
                         : Route{.Net = *_System, .Inner = held.Inner};
}

MCSocket::Handle MCNetSwitch::Keep(const Route& route, MCSocket::Handle inner)
{
    if (inner == MCSocket::InvalidHandle)
    {
        return MCSocket::InvalidHandle;
    }

    {
        const std::scoped_lock lock(_Lock);

        // A system's socket is always kept; an internet game's only while that game is on.
        if (route.Internet == nullptr || route.Internet == _Internet)
        {
            const MCSocket::Handle handle = _NextHandle++;
            _Held.emplace(handle, Held{.Internet = route.Internet != nullptr, .Inner = inner});
            return handle;
        }
    }

    // Its internet game ended while it opened.
    route.Net.Close(inner);
    return MCSocket::InvalidHandle;
}

bool MCNetSwitch::Startup()
{
    return Current().Net.Startup();
}

std::optional<uint32_t> MCNetSwitch::Resolve(std::string_view host)
{
    return Current().Net.Resolve(host);
}

MCSocket::Handle MCNetSwitch::ListenTcp(uint16_t port)
{
    const Route route = Current();
    return Keep(route, route.Net.ListenTcp(port));
}

MCSocket::Handle MCNetSwitch::AcceptTcp(MCSocket::Handle listener, MCSocket::Address& from)
{
    const std::optional<Route> route = Find(listener);
    return route.has_value() ? Keep(*route, route->Net.AcceptTcp(route->Inner, from)) : MCSocket::InvalidHandle;
}

MCSocket::Handle MCNetSwitch::ConnectTcp(const MCSocket::Address& to, int timeoutMs)
{
    const Route route = Current();
    return Keep(route, route.Net.ConnectTcp(to, timeoutMs));
}

void MCNetSwitch::KeepAlive(MCSocket::Handle socket)
{
    if (const std::optional<Route> route = Find(socket))
    {
        route->Net.KeepAlive(route->Inner);
    }
}

int MCNetSwitch::Send(MCSocket::Handle socket, std::span<const uint8_t> data)
{
    const std::optional<Route> route = Find(socket);
    return route.has_value() ? route->Net.Send(route->Inner, data) : -1;
}

int MCNetSwitch::Receive(MCSocket::Handle socket, std::span<uint8_t> data)
{
    const std::optional<Route> route = Find(socket);
    return route.has_value() ? route->Net.Receive(route->Inner, data) : -1;
}

MCSocket::Handle MCNetSwitch::OpenUdp(uint16_t port)
{
    const Route route = Current();
    return Keep(route, route.Net.OpenUdp(port));
}

bool MCNetSwitch::SendTo(MCSocket::Handle socket, const MCSocket::Address& to, std::span<const uint8_t> data)
{
    const std::optional<Route> route = Find(socket);
    return route.has_value() && route->Net.SendTo(route->Inner, to, data);
}

int MCNetSwitch::ReceiveFrom(MCSocket::Handle socket, std::span<uint8_t> data, MCSocket::Address& from)
{
    const std::optional<Route> route = Find(socket);
    return route.has_value() ? route->Net.ReceiveFrom(route->Inner, data, from) : -1;
}

uint16_t MCNetSwitch::LocalPort(MCSocket::Handle socket)
{
    const std::optional<Route> route = Find(socket);
    return route.has_value() ? route->Net.LocalPort(route->Inner) : 0;
}

bool MCNetSwitch::EndSending(MCSocket::Handle socket)
{
    const std::optional<Route> route = Find(socket);
    return route.has_value() && route->Net.EndSending(route->Inner);
}

void MCNetSwitch::Close(MCSocket::Handle socket)
{
    std::optional<Route> route;

    {
        const std::scoped_lock lock(_Lock);
        const auto found = _Held.find(socket);

        if (found == _Held.end())
        {
            return;
        }

        route.emplace(RouteOf(found->second));
        _Held.erase(found);
    }

    route->Net.Close(route->Inner);
}
