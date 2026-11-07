#include "stdafx.h"
#include "platform/MCIceTransport.h"

namespace
{
    /// <summary>What a frame on a link is (its first byte).</summary>
    enum FrameKind : uint8_t
    {
        /// <summary>A datagram: the sender's port, the port it's for, its bytes.</summary>
        DatagramFrame = 1,
        /// <summary>A guest's connection: its number on the link, the port it's for, the guest's port.</summary>
        ConnectFrame = 2,
        /// <summary>The host took the connection numbered.</summary>
        AcceptFrame = 3,
        /// <summary>Nothing listens on the port it was for, or the listener is full.</summary>
        RefuseFrame = 4,
        /// <summary>A connection's bytes: its number, the bytes.</summary>
        DataFrame = 5,
        /// <summary>The sender closed the connection numbered.</summary>
        CloseFrame = 6,
    };

    /// <summary>A frame's kind and the size of what follows (4 bytes, little-endian).</summary>
    constexpr size_t FrameHeadBytes = 5;
    constexpr size_t StreamBytes = 4;
    constexpr size_t PortsBytes = 4;
    /// <summary>The largest datagram, as UDP's over IPv4.</summary>
    constexpr size_t MaxDatagramBytes = 65507;
    /// <summary>The largest frame after its head: a datagram's.</summary>
    constexpr size_t MaxFrameBody = MaxDatagramBytes + PortsBytes;
    /// <summary>A connection's bytes go in frames of up to this, so a big send never holds up a link's datagrams.</summary>
    constexpr size_t DataPieceBytes = 16 * 1024;
    /// <summary>
    /// A connection takes no more bytes while its link holds this many unsent, so a game that stops reading is dropped
    /// by the sender's own limit (MCDirectPlay's), as over TCP.
    /// </summary>
    constexpr size_t SendWindowBytes = 1u << 20;
    /// <summary>The datagrams a link keeps for the time it opens; more are lost, as UDP's.</summary>
    constexpr size_t MaxEarlyBytes = 64 * 1024;
    /// <summary>The datagrams a UDP socket keeps unread; more are lost, as UDP's.</summary>
    constexpr size_t MaxQueuedDatagrams = 64;
    /// <summary>The guests' links a host keeps at once (a match has 6 players); more offers go unanswered.</summary>
    constexpr size_t MaxHostLinks = 16;
    /// <summary>The links one guest keeps with a host at once: its link, and a new one while the old one closes.</summary>
    constexpr size_t MaxLinksPerGuest = 2;
    /// <summary>
    /// A connection's bytes the game hasn't read. A game's largest message is 5 KB (MsgBufferSize) and the game reads
    /// everything each frame, so more is a flood, and the link is cut: as much as the link itself holds unread
    /// (MCIceLink's), and a Mac's TCP takes at most 4 MB.
    /// </summary>
    constexpr size_t MaxUnreadBytes = 1u << 20;
    /// <summary>How long a link nothing uses waits for what was sent on it to go before it closes, as TCP's close.</summary>
    constexpr auto LingerDeadline = std::chrono::seconds(2);
    /// <summary>The connections one link carries at once (a guest makes one).</summary>
    constexpr size_t MaxStreamsPerLink = 4;
    /// <summary>The connections a listener takes before they're accepted.</summary>
    constexpr size_t MaxPendingAccepts = 8;
    /// <summary>The first address a game or guest link is given: 0.0.0.0/8 is never a machine's.</summary>
    constexpr uint32_t FirstLinkIp = 1;
    /// <summary>The first port a socket opened on port 0 is given (a connection's too).</summary>
    constexpr uint16_t FirstEphemeralPort = 50000;
    /// <summary>How long a connection waits between looks at its link.</summary>
    constexpr auto ConnectStep = std::chrono::milliseconds(2);
    /// <summary>The bytes read from a link at once.</summary>
    constexpr size_t ReadChunkBytes = 16 * 1024;
    /// <summary>A link's first signal from a guest (MCIceLink's offer).</summary>
    constexpr std::string_view OfferKind = "offer";
    /// <summary>
    /// The transport's own signals on a link, which never reach ICE: the service carries the link from now (the
    /// WebSocket path), and the sender closed a link the service carries.
    /// </summary>
    constexpr std::string_view ServiceKind = "service";
    constexpr std::string_view CloseKind = "close";
    /// <summary>The bytes the service carries in one message (its Worker takes 64 KB).</summary>
    constexpr size_t ServicePieceBytes = 16 * 1024;
    /// <summary>The bytes a link the service carries holds unread, as an ICE link holds (MCIceLink's 1 MB).</summary>
    constexpr size_t MaxServiceUnreadBytes = 1u << 20;

    void PutU16(std::vector<uint8_t>& out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>(value));
        out.push_back(static_cast<uint8_t>(value >> 8));
    }

    void PutU32(std::vector<uint8_t>& out, uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            out.push_back(static_cast<uint8_t>(value >> shift));
        }
    }

    uint16_t U16(std::span<const uint8_t> bytes, size_t at)
    {
        return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
    }

    uint32_t U32(std::span<const uint8_t> bytes, size_t at)
    {
        return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    }

    /// <summary>A connection's number, as its frames carry it.</summary>
    std::vector<uint8_t> StreamHead(uint32_t stream)
    {
        std::vector<uint8_t> head;
        PutU32(head, stream);
        return head;
    }

    /// <summary>A link as the log names it: by its game, or as a guest's (never an address).</summary>
    std::string LinkName(bool guest, std::string_view game)
    {
        return guest ? std::format("the link to game \"{}\"", game) : std::string("a guest's link");
    }

    const char* PathText(MCIceLink::Path path)
    {
        switch (path)
        {
            case MCIceLink::Path::DirectIpv6:
                return "direct, IPv6";
            case MCIceLink::Path::DirectIpv4:
                return "direct, IPv4";
            case MCIceLink::Path::Relay:
                return "through the relay";
            case MCIceLink::Path::Service:
                return "through the service";
            case MCIceLink::Path::None:
                break;
        }

        return "by a path not known";
    }
}

MCIceTransport::MCIceTransport(std::shared_ptr<MCIceSignaling> signaling)
    : MCIceTransport(std::move(signaling), Deadlines{})
{
}

MCIceTransport::MCIceTransport(std::shared_ptr<MCIceSignaling> signaling, Deadlines deadlines, LinkReports reports)
    : _Signaling(std::move(signaling))
    , _Deadlines(deadlines)
    , _Reports(std::move(reports))
    , _NextPort(FirstEphemeralPort)
    , _NextIp(FirstLinkIp)
{
}

MCIceTransport::~MCIceTransport()
{
    // Each link closes as it goes, so the other ends see it closed.
    const std::scoped_lock lock(_Lock);

    while (!_Links.empty())
    {
        EndLink(_Links.begin()->first, MCTelemetry::MCLinkEnd::Left, "this game left");
    }
}

size_t MCIceTransport::Links() const
{
    const std::scoped_lock lock(_Lock);
    return _Links.size();
}

bool MCIceTransport::Startup()
{
    return true;
}

std::optional<uint32_t> MCIceTransport::Resolve(std::string_view host)
{
    if (host.empty())
    {
        return std::nullopt;
    }

    const std::scoped_lock lock(_Lock);

    if (const auto found = _GameIps.find(host); found != _GameIps.end())
    {
        return found->second;
    }

    const uint32_t ip = _NextIp++;
    _GameIps.emplace(std::string(host), ip);
    _GameNames.emplace(ip, std::string(host));
    return ip;
}

MCSocket::Handle MCIceTransport::ListenTcp(uint16_t port)
{
    const std::scoped_lock lock(_Lock);

    if (port != 0 && PortTaken(Kind::Listener, port))
    {
        return MCSocket::InvalidHandle;
    }

    return Open(Kind::Listener, port);
}

MCSocket::Handle MCIceTransport::AcceptTcp(MCSocket::Handle listener, MCSocket::Address& from)
{
    const std::scoped_lock lock(_Lock);
    Pump();
    const MCOptionalRef<Socket> socket = Find(listener, Kind::Listener);

    if (!socket || socket->get().Pending.empty())
    {
        return MCSocket::InvalidHandle;
    }

    const MCSocket::Handle accepted = socket->get().Pending.front();
    socket->get().Pending.pop_front();
    const Socket& stream = _Sockets.at(accepted);
    const auto link = _Links.find(stream.Link);
    from = MCSocket::Address{link != _Links.end() ? link->second.Ip : 0, stream.PeerPort};
    return accepted;
}

MCSocket::Handle MCIceTransport::ConnectTcp(const MCSocket::Address& to, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    MCSocket::Handle handle = MCSocket::InvalidHandle;

    {
        const std::scoped_lock lock(_Lock);
        Pump();
        const std::optional<uint32_t> id = LinkToGame(to.Ip);

        if (!id.has_value())
        {
            return MCSocket::InvalidHandle;
        }

        Link& link = _Links.at(*id);
        handle = Open(Kind::Stream, 0);
        Socket& stream = _Sockets.at(handle);
        stream.Link = *id;
        stream.Stream = link.NextStream++;
        stream.PeerPort = to.Port;
        std::vector<uint8_t> head = StreamHead(stream.Stream);
        PutU16(head, to.Port);
        PutU16(head, stream.Port);
        SendFrame(link, ConnectFrame, head);
    }

    for (;;)
    {
        {
            const std::scoped_lock lock(_Lock);
            Pump();
            const MCOptionalRef<Socket> stream = Find(handle, Kind::Stream);

            // Taken by the host, even if closed since: its bytes, then its close, are the caller's to read, as TCP's.
            if (stream && stream->get().Accepted)
            {
                return handle;
            }

            if (!stream || stream->get().State != StreamState::Asked || std::chrono::steady_clock::now() >= deadline)
            {
                SDL_Log("MCIceTransport: connecting to game \"%s\" (port %u) failed: %s",
                        _GameNames.contains(to.Ip) ? _GameNames.at(to.Ip).c_str() : "?", to.Port,
                        stream && stream->get().State == StreamState::Refused ? "its host refused it"
                        : stream && stream->get().State == StreamState::Asked ? "no answer in time"
                                                                              : "its link failed");
                CloseLocked(handle);
                DropUnused();
                return MCSocket::InvalidHandle;
            }
        }

        std::this_thread::sleep_for(ConnectStep);
    }
}

void MCIceTransport::KeepAlive(MCSocket::Handle)
{
}

int MCIceTransport::Send(MCSocket::Handle socket, std::span<const uint8_t> data)
{
    const std::scoped_lock lock(_Lock);
    Pump();
    const MCOptionalRef<Socket> stream = Find(socket, Kind::Stream);

    if (!stream || stream->get().State != StreamState::Open)
    {
        return stream && stream->get().State == StreamState::Asked ? 0 : -1;
    }

    const auto found = _Links.find(stream->get().Link);

    if (found == _Links.end())
    {
        return -1;
    }

    Link& link = found->second;
    const size_t unsent = Unsent(link);

    if (unsent >= SendWindowBytes)
    {
        return 0;
    }

    const size_t taken = std::min(data.size(), SendWindowBytes - unsent);
    const std::vector<uint8_t> head = StreamHead(stream->get().Stream);

    for (size_t at = 0; at < taken; at += DataPieceBytes)
    {
        SendFrame(link, DataFrame, head, data.subspan(at, std::min(DataPieceBytes, taken - at)));
    }

    return static_cast<int>(taken);
}

int MCIceTransport::Receive(MCSocket::Handle socket, std::span<uint8_t> data)
{
    const std::scoped_lock lock(_Lock);
    Pump();
    const MCOptionalRef<Socket> stream = Find(socket, Kind::Stream);

    if (!stream)
    {
        return -1;
    }

    std::deque<uint8_t>& inbox = stream->get().Inbox;

    if (inbox.empty())
    {
        return stream->get().State == StreamState::Open || stream->get().State == StreamState::Asked ? 0 : -1;
    }

    const size_t count = std::min(data.size(), inbox.size());
    std::copy_n(inbox.begin(), count, data.begin());
    inbox.erase(inbox.begin(), inbox.begin() + static_cast<std::ptrdiff_t>(count));
    return static_cast<int>(count);
}

MCSocket::Handle MCIceTransport::OpenUdp(uint16_t port)
{
    const std::scoped_lock lock(_Lock);

    if (port != 0 && PortTaken(Kind::Udp, port))
    {
        return MCSocket::InvalidHandle;
    }

    return Open(Kind::Udp, port);
}

bool MCIceTransport::SendTo(MCSocket::Handle socket, const MCSocket::Address& to, std::span<const uint8_t> data)
{
    const std::scoped_lock lock(_Lock);
    Pump();
    const MCOptionalRef<Socket> udp = Find(socket, Kind::Udp);

    if (!udp || data.size() > MaxDatagramBytes)
    {
        return false;
    }

    const std::optional<uint32_t> id = _GameNames.contains(to.Ip) ? LinkToGame(to.Ip) : LinkAt(to.Ip);

    if (!id.has_value())
    {
        return false;
    }

    Link& link = _Links.at(*id);

    if (link.Guest)
    {
        link.Senders.insert(socket);
    }

    std::vector<uint8_t> head;
    PutU16(head, udp->get().Port);
    PutU16(head, to.Port);
    SendFrame(link, DatagramFrame, head, data);
    return true;
}

int MCIceTransport::ReceiveFrom(MCSocket::Handle socket, std::span<uint8_t> data, MCSocket::Address& from)
{
    const std::scoped_lock lock(_Lock);
    Pump();
    const MCOptionalRef<Socket> udp = Find(socket, Kind::Udp);

    if (!udp)
    {
        return -1;
    }

    std::deque<Datagram>& datagrams = udp->get().Datagrams;

    if (datagrams.empty())
    {
        return 0;
    }

    const Datagram datagram = std::move(datagrams.front());
    datagrams.pop_front();
    const size_t count = std::min(data.size(), datagram.Bytes.size());
    std::copy_n(datagram.Bytes.begin(), count, data.begin());
    from = datagram.From;
    return static_cast<int>(count);
}

uint16_t MCIceTransport::LocalPort(MCSocket::Handle socket)
{
    const std::scoped_lock lock(_Lock);
    const auto found = _Sockets.find(socket);
    return found != _Sockets.end() ? found->second.Port : 0;
}

void MCIceTransport::Close(MCSocket::Handle socket)
{
    const std::scoped_lock lock(_Lock);
    CloseLocked(socket);
    DropUnused();
}

void MCIceTransport::CloseLocked(MCSocket::Handle handle)
{
    const auto found = _Sockets.find(handle);

    if (found == _Sockets.end())
    {
        return;
    }

    Socket& socket = found->second;
    std::deque<MCSocket::Handle> pending;

    if (socket.Type == Kind::Stream && (socket.State == StreamState::Asked || socket.State == StreamState::Open))
    {
        if (const auto link = _Links.find(socket.Link); link != _Links.end())
        {
            SendFrame(link->second, CloseFrame, StreamHead(socket.Stream));
        }
    }
    else if (socket.Type == Kind::Listener)
    {
        pending = std::move(socket.Pending);
    }
    else if (socket.Type == Kind::Udp)
    {
        for (auto& [id, link] : _Links)
        {
            link.Senders.erase(handle);
        }
    }

    _Sockets.erase(found);

    // The connections it took and no one accepted close with it.
    for (const MCSocket::Handle stream : pending)
    {
        CloseLocked(stream);
    }
}

void MCIceTransport::Pump()
{
    for (const MCIceSignaling::Heard& heard : _Signaling->Take())
    {
        Hear(heard);
    }

    std::vector<uint32_t> ids;

    for (const auto& [id, link] : _Links)
    {
        ids.push_back(id);
    }

    for (const uint32_t id : ids)
    {
        const auto found = _Links.find(id);

        if (found == _Links.end())
        {
            continue;
        }

        Link& link = found->second;

        // The service carries it: its frames to read, and its end comes through the service (a close, or the peer gone).
        if (link.ServiceSince.has_value())
        {
            if (!ReadFrames(id, link))
            {
                EndLink(id, MCTelemetry::MCLinkEnd::CutOff, "it sent what this game can't take");
            }

            continue;
        }

        for (const MCIceLink::Signal& signal : link.Ice->TakeSignals())
        {
            SendSignal(link, signal);
        }

        const MCIceLink::State state = link.Ice->GetState();

        const auto now = std::chrono::steady_clock::now();

        if (state == MCIceLink::State::Open && !link.WasOpen)
        {
            link.WasOpen = true;
            link.IdleSince = now;
            link.Path = link.Ice->GetPath();
            const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(now - link.Started);
            SDL_Log("MCIceTransport: %s opened in %lld ms, %s", LinkName(link.Guest, link.Peer).c_str(),
                    static_cast<long long>(took.count()), PathText(link.Path));
        }

        // Read while the link lives: libdatachannel's counts go with its transports as it ends.
        if (link.WasOpen)
        {
            link.Ice->Carried();
        }

        if (state == MCIceLink::State::Open && !link.Early.empty())
        {
            link.Ice->Send(link.Early);
            link.Early.clear();
        }

        // What arrived before a close is read first: a connection's last bytes, then its close.
        if (!ReadFrames(id, link))
        {
            EndLink(id, MCTelemetry::MCLinkEnd::CutOff, "it sent what this game can't take");
        }
        else if (state == MCIceLink::State::Failed && _Deadlines.Service.has_value() && link.Heard &&
                 !link.Ice->OpenedAt() && !link.Ice->FoundPath() && !link.Ice->CutOff())
        {
            ToService(link, true, "ICE found no path");
        }
        else if (state == MCIceLink::State::Connecting && _Deadlines.Service.has_value() && link.Heard &&
                 !link.Ice->FoundPath() && now - link.HeardAt > *_Deadlines.Service)
        {
            ToService(link, true, "ICE found no path in time");
        }
        else if (state == MCIceLink::State::Failed || state == MCIceLink::State::Closed)
        {
            const bool failed = state == MCIceLink::State::Failed;
            const MCTelemetry::MCLinkEnd end = failed && link.Ice->CutOff() ? MCTelemetry::MCLinkEnd::CutOff
                                               : !link.Ice->OpenedAt()      ? Unopened(link)
                                               : failed                     ? MCTelemetry::MCLinkEnd::Lost
                                                                            : MCTelemetry::MCLinkEnd::Closed;
            EndLink(id, end, failed ? "it failed" : "it closed");
        }
        else if (state == MCIceLink::State::Connecting && now - link.Started > _Deadlines.Open)
        {
            EndLink(id, Unopened(link), "it didn't open in time");
        }
    }

    DropUnused();
}

void MCIceTransport::Hear(const MCIceSignaling::Heard& heard)
{
    const bool fromGuest = heard.Sender == MCIceSignaling::From::Guest;

    if (!heard.Signal.has_value() && heard.Bytes.empty())
    {
        // The peer left the service, or the game was never there: its links still opening can't now, and those the
        // service carries are gone.
        std::vector<uint32_t> ended;

        for (const auto& [id, link] : _Links)
        {
            if (link.Guest != fromGuest && link.Peer == heard.Peer &&
                (!link.Ice->OpenedAt() || link.ServiceSince.has_value()))
            {
                ended.push_back(id);
            }
        }

        for (const uint32_t id : ended)
        {
            if (_Links.at(id).ServiceSince.has_value())
            {
                EndLink(id, MCTelemetry::MCLinkEnd::Lost,
                        fromGuest ? "the guest left the service" : "the game left the service");
            }
            else if (fromGuest)
            {
                EndLink(id, MCTelemetry::MCLinkEnd::GuestLeft, "the guest left the service");
            }
            else
            {
                EndLink(id, MCTelemetry::MCLinkEnd::NoGame, "the service has no such game");
            }
        }

        return;
    }

    if (!fromGuest)
    {
        // A guest end's link is numbered by this transport.
        const auto found = _Links.find(heard.Link);

        if (found != _Links.end() && found->second.Guest && found->second.Peer == heard.Peer)
        {
            HearOnLink(found->first, found->second, heard);
        }

        return;
    }

    auto found = std::ranges::find_if(
        _Links, [&](const auto& entry)
        { return !entry.second.Guest && entry.second.Peer == heard.Peer && entry.second.Number == heard.Link; });

    if (found == _Links.end())
    {
        // Only an offer starts a link, and only while this game listens: anything else is a closed link's last words.
        if (!heard.Signal.has_value() || heard.Signal->Kind != OfferKind || !Listening())
        {
            return;
        }

        if (std::ranges::count_if(_Links, [&](const auto& entry)
                                  { return !entry.second.Guest && entry.second.Peer == heard.Peer; }) >=
            static_cast<std::ptrdiff_t>(MaxLinksPerGuest))
        {
            SDL_Log("MCIceTransport: a guest's link left unanswered: that guest has %zu links open already",
                    MaxLinksPerGuest);
            return;
        }

        if (std::ranges::count_if(_Links, [](const auto& entry) { return !entry.second.Guest; }) >=
            static_cast<std::ptrdiff_t>(MaxHostLinks))
        {
            SDL_Log("MCIceTransport: a guest's link left unanswered: %zu guests' links are open already", MaxHostLinks);
            return;
        }

        std::unique_ptr<MCIceLink> ice = MCIceLink::Answer(_Signaling->LinkSettings());

        if (ice == nullptr)
        {
            return;
        }

        const uint32_t id = _NextLink++;
        Link link;
        link.Ice = std::move(ice);
        link.Peer = heard.Peer;
        link.Number = heard.Link;
        link.Ip = _NextIp++;
        link.Started = std::chrono::steady_clock::now();
        found = _Links.emplace(id, std::move(link)).first;
    }

    HearOnLink(found->first, found->second, heard);
}

void MCIceTransport::HearOnLink(uint32_t id, Link& link, const MCIceSignaling::Heard& heard)
{
    if (!heard.Signal.has_value())
    {
        // Bytes for a link the service doesn't carry are a closed link's last words.
        if (link.ServiceSince.has_value() && !HearBytes(link, heard.Bytes))
        {
            EndLink(id, MCTelemetry::MCLinkEnd::CutOff, "the service brought more than this game holds unread");
        }

        return;
    }

    if (!link.Heard)
    {
        link.Heard = true;
        link.HeardAt = std::chrono::steady_clock::now();
    }

    // ICE's signals for a link the service carries came too late to matter.
    if (!HearTransportSignal(id, link, *heard.Signal) && !link.ServiceSince.has_value())
    {
        link.Ice->Heard(*heard.Signal);
    }
}

bool MCIceTransport::HearTransportSignal(uint32_t id, Link& link, const MCIceLink::Signal& signal)
{
    if (signal.Kind == ServiceKind)
    {
        if (link.ServiceSince.has_value())
        {
            return true;
        }

        // What this end sent over ICE's channel would be lost: the link ends, and the game asks again.
        if (link.Ice->OpenedAt().has_value())
        {
            EndLink(id, MCTelemetry::MCLinkEnd::Lost, "the other end took the service's path after this end opened");
            return true;
        }

        ToService(link, false, "the other end found no path");
        return true;
    }

    if (signal.Kind == CloseKind)
    {
        // An ICE link's close is ICE's own.
        if (link.ServiceSince.has_value())
        {
            EndLink(id, MCTelemetry::MCLinkEnd::Closed, "the other end closed it");
        }

        return true;
    }

    return false;
}

bool MCIceTransport::HearBytes(Link& link, std::span<const uint8_t> bytes)
{
    if (link.Input.size() + bytes.size() > MaxServiceUnreadBytes)
    {
        SDL_Log("MCIceTransport: %s through the service got over %zu bytes unread",
                LinkName(link.Guest, link.Peer).c_str(), MaxServiceUnreadBytes);
        return false;
    }

    link.Input.insert(link.Input.end(), bytes.begin(), bytes.end());
    link.ServiceCarried.BytesReceived += bytes.size();
    return true;
}

void MCIceTransport::ToService(Link& link, bool tell, std::string_view why)
{
    const auto now = std::chrono::steady_clock::now();
    SDL_Log("MCIceTransport: the service carries %s from now: %.*s", LinkName(link.Guest, link.Peer).c_str(),
            static_cast<int>(why.size()), why.data());

    // The other end hears it before any of the link's bytes: the service keeps a player's signals and bytes in order.
    if (tell)
    {
        SendSignal(link, {.Kind = std::string(ServiceKind)});
    }

    link.ServiceSince = now;
    link.WasOpen = true;
    link.IdleSince = now;
    link.Path = MCIceLink::Path::Service;
    link.Ice->Close();

    if (!link.Early.empty())
    {
        SendBytes(link, link.Early);
        link.Early.clear();
    }
}

void MCIceTransport::SendSignal(const Link& link, const MCIceLink::Signal& signal)
{
    if (link.Guest)
    {
        _Signaling->ToHost(link.Peer, link.Number, signal);
    }
    else
    {
        _Signaling->ToGuest(link.Peer, link.Number, signal);
    }
}

void MCIceTransport::SendBytes(Link& link, std::span<const uint8_t> bytes)
{
    if (!link.ServiceSince.has_value())
    {
        link.Ice->Send(bytes);
        return;
    }

    for (size_t at = 0; at < bytes.size(); at += ServicePieceBytes)
    {
        const std::span<const uint8_t> piece = bytes.subspan(at, std::min(ServicePieceBytes, bytes.size() - at));

        if (link.Guest)
        {
            _Signaling->BytesToHost(link.Peer, link.Number, piece);
        }
        else
        {
            _Signaling->BytesToGuest(link.Peer, link.Number, piece);
        }
    }

    link.ServiceCarried.BytesSent += bytes.size();
}

bool MCIceTransport::IsOpen(const Link& link)
{
    return link.ServiceSince.has_value() || link.Ice->GetState() == MCIceLink::State::Open;
}

size_t MCIceTransport::Unsent(const Link& link)
{
    return link.ServiceSince.has_value() ? 0 : link.Ice->Unsent();
}

bool MCIceTransport::ReadFrames(uint32_t id, Link& link)
{
    std::array<uint8_t, ReadChunkBytes> chunk{};

    for (size_t got = 0; (got = link.Ice->Receive(chunk)) != 0;)
    {
        link.Input.insert(link.Input.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }

    const std::span<const uint8_t> input(link.Input);
    size_t at = 0;

    while (input.size() - at >= FrameHeadBytes)
    {
        const uint8_t kind = input[at];
        const uint32_t size = U32(input, at + 1);

        if (size > MaxFrameBody)
        {
            return false;
        }

        if (input.size() - at - FrameHeadBytes < size)
        {
            break;
        }

        if (!HandleFrame(id, link, kind, input.subspan(at + FrameHeadBytes, size)))
        {
            return false;
        }

        at += FrameHeadBytes + size;
    }

    link.Input.erase(link.Input.begin(), link.Input.begin() + static_cast<std::ptrdiff_t>(at));
    return true;
}

bool MCIceTransport::HandleFrame(uint32_t id, Link& link, uint8_t kind, std::span<const uint8_t> body)
{
    if (kind == DatagramFrame)
    {
        if (body.size() < PortsBytes)
        {
            return false;
        }

        const uint16_t to = U16(body, 2);
        const auto udp = std::ranges::find_if(_Sockets, [&](const auto& entry)
                                              { return entry.second.Type == Kind::Udp && entry.second.Port == to; });

        if (udp != _Sockets.end() && udp->second.Datagrams.size() < MaxQueuedDatagrams)
        {
            const std::span<const uint8_t> bytes = body.subspan(PortsBytes);
            udp->second.Datagrams.push_back({{link.Ip, U16(body, 0)}, {bytes.begin(), bytes.end()}});
        }

        return true;
    }

    if (body.size() < StreamBytes)
    {
        return false;
    }

    const uint32_t number = U32(body, 0);
    const MCOptionalRef<Socket> stream = StreamOn(id, number);

    switch (kind)
    {
        case ConnectFrame:
        {
            if (body.size() != StreamBytes + PortsBytes)
            {
                return false;
            }

            const uint16_t to = U16(body, StreamBytes);
            const auto listener =
                std::ranges::find_if(_Sockets, [&](const auto& entry)
                                     { return entry.second.Type == Kind::Listener && entry.second.Port == to; });
            const auto streams =
                std::ranges::count_if(_Sockets, [&](const auto& entry)
                                      { return entry.second.Type == Kind::Stream && entry.second.Link == id; });

            // Only a guest connects, to a port listened on, once by each number.
            if (link.Guest || stream || listener == _Sockets.end() ||
                listener->second.Pending.size() >= MaxPendingAccepts ||
                streams >= static_cast<std::ptrdiff_t>(MaxStreamsPerLink))
            {
                SendFrame(link, RefuseFrame, StreamHead(number));
                return true;
            }

            const MCSocket::Handle handle = Open(Kind::Stream, to);
            Socket& accepted = _Sockets.at(handle);
            accepted.Link = id;
            accepted.Stream = number;
            accepted.PeerPort = U16(body, StreamBytes + 2);
            accepted.State = StreamState::Open;
            // Open's insert leaves the listener's entry where it was.
            listener->second.Pending.push_back(handle);
            SendFrame(link, AcceptFrame, StreamHead(number));
            return true;
        }

        case AcceptFrame:
        case RefuseFrame:
        case CloseFrame:
        {
            if (body.size() != StreamBytes)
            {
                return false;
            }

            if (!stream)
            {
                // A connection closed here meanwhile.
                return true;
            }

            StreamState& state = stream->get().State;

            if (kind == CloseFrame && (state == StreamState::Asked || state == StreamState::Open))
            {
                state = StreamState::Ended;
            }
            else if (state == StreamState::Asked)
            {
                state = kind == AcceptFrame ? StreamState::Open : StreamState::Refused;
                stream->get().Accepted = kind == AcceptFrame;
            }

            return true;
        }

        case DataFrame:
        {
            if (stream && stream->get().State == StreamState::Open)
            {
                const std::span<const uint8_t> bytes = body.subspan(StreamBytes);
                std::deque<uint8_t>& inbox = stream->get().Inbox;

                if (inbox.size() + bytes.size() > MaxUnreadBytes)
                {
                    SDL_Log("MCIceTransport: a connection got over %zu bytes the game hadn't read", MaxUnreadBytes);
                    return false;
                }

                inbox.insert(inbox.end(), bytes.begin(), bytes.end());
            }

            return true;
        }

        default:
        {
            return false;
        }
    }
}

void MCIceTransport::SendFrame(Link& link, uint8_t kind, std::span<const uint8_t> head, std::span<const uint8_t> bytes)
{
    const size_t size = head.size() + bytes.size();
    const bool open = IsOpen(link);

    // Before the link opens a datagram waits only while there's room, as a system's UDP buffer; the rest always wait.
    if (!open && kind == DatagramFrame && link.Early.size() + FrameHeadBytes + size > MaxEarlyBytes)
    {
        return;
    }

    // After any that waited for the link: a link that opened since the last pump sends them first.
    std::vector<uint8_t>& out = link.Early;
    out.push_back(kind);
    PutU32(out, static_cast<uint32_t>(size));
    out.insert(out.end(), head.begin(), head.end());
    out.insert(out.end(), bytes.begin(), bytes.end());

    // A send that fails has failed the link, which the next pump ends.
    if (open)
    {
        SendBytes(link, out);
        out.clear();
    }
}

void MCIceTransport::EndLink(uint32_t id, MCTelemetry::MCLinkEnd end, std::string_view why)
{
    const auto found = _Links.find(id);

    if (found == _Links.end())
    {
        return;
    }

    Link& link = found->second;

    SDL_Log("MCIceTransport: %s ended: %.*s", LinkName(link.Guest, link.Peer).c_str(), static_cast<int>(why.size()),
            why.data());
    const auto now = std::chrono::steady_clock::now();
    const bool service = link.ServiceSince.has_value();
    // Opened as libdatachannel saw it (a link can open and end between two pumps), or as the service took it.
    const std::optional<std::chrono::steady_clock::time_point> openedAt =
        service ? link.ServiceSince : link.Ice->OpenedAt();
    _Reports({
        .Guest = link.Guest,
        .Opened = openedAt.has_value(),
        .Path = link.WasOpen ? link.Path : link.Ice->GetPath(),
        .Setup = std::chrono::duration_cast<std::chrono::milliseconds>(openedAt.value_or(now) - link.Started),
        .Open = std::chrono::duration_cast<std::chrono::milliseconds>(now - openedAt.value_or(now)),
        .Carried = service ? link.ServiceCarried : link.Ice->Carried(),
        .Here = link.Ice->OfferedHere(),
        .There = link.Ice->OfferedThere(),
        .End = end,
    });

    for (auto& [handle, socket] : _Sockets)
    {
        if (socket.Type == Kind::Stream && socket.Link == id &&
            (socket.State == StreamState::Asked || socket.State == StreamState::Open))
        {
            socket.State = StreamState::Broken;
        }
    }

    // The other end of a link the service carries hears of its close only from this end (its ICE link closed already).
    if (service)
    {
        SendSignal(link, {.Kind = std::string(CloseKind)});
    }

    link.Ice->Close();
    _Links.erase(found);
}

void MCIceTransport::DropUnused()
{
    const bool listening = Listening();
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::pair<uint32_t, std::string_view>> unused;

    for (auto& [id, link] : _Links)
    {
        const bool carries = std::ranges::any_of(
            _Sockets, [&](const auto& entry) { return entry.second.Type == Kind::Stream && entry.second.Link == id; });

        if (carries)
        {
            link.IdleSince = now;
        }

        const bool idle = !link.Guest && link.WasOpen && !carries && now - link.IdleSince > _Deadlines.Idle;

        if (!idle && (carries || (link.Guest ? !link.Senders.empty() : listening)))
        {
            link.UnusedSince.reset();
            continue;
        }

        if (!link.UnusedSince.has_value())
        {
            link.UnusedSince = now;
        }

        if (Unsent(link) == 0 || now - *link.UnusedSince > LingerDeadline)
        {
            unused.emplace_back(id, idle         ? "it carried no connection for a while"
                                    : link.Guest ? "nothing here uses it"
                                                 : "this game stopped hosting");
        }
    }

    for (const auto& [id, why] : unused)
    {
        EndLink(id, MCTelemetry::MCLinkEnd::Unused, why);
    }
}

MCTelemetry::MCLinkEnd MCIceTransport::Unopened(const Link& link)
{
    if (link.Ice->FoundPath())
    {
        return MCTelemetry::MCLinkEnd::NoChannel;
    }

    return link.Heard ? MCTelemetry::MCLinkEnd::NoPath : MCTelemetry::MCLinkEnd::Unanswered;
}

std::optional<uint32_t> MCIceTransport::LinkToGame(uint32_t ip)
{
    const auto name = _GameNames.find(ip);

    if (name == _GameNames.end())
    {
        return std::nullopt;
    }

    for (const auto& [id, link] : _Links)
    {
        if (link.Guest && link.Ip == ip)
        {
            return id;
        }
    }

    std::unique_ptr<MCIceLink> ice = MCIceLink::Offer(_Signaling->LinkSettings());

    if (ice == nullptr)
    {
        return std::nullopt;
    }

    const uint32_t id = _NextLink++;
    Link link;
    link.Ice = std::move(ice);
    link.Guest = true;
    link.Peer = name->second;
    link.Number = id;
    link.Ip = ip;
    link.Started = std::chrono::steady_clock::now();
    _Links.emplace(id, std::move(link));
    return id;
}

std::optional<uint32_t> MCIceTransport::LinkAt(uint32_t ip) const
{
    for (const auto& [id, link] : _Links)
    {
        if (!link.Guest && link.Ip == ip)
        {
            return id;
        }
    }

    return std::nullopt;
}

MCOptionalRef<MCIceTransport::Socket> MCIceTransport::StreamOn(uint32_t link, uint32_t stream)
{
    for (auto& [handle, socket] : _Sockets)
    {
        if (socket.Type == Kind::Stream && socket.Link == link && socket.Stream == stream)
        {
            return socket;
        }
    }

    return std::nullopt;
}

bool MCIceTransport::Listening() const
{
    return std::ranges::any_of(_Sockets, [](const auto& entry) { return entry.second.Type == Kind::Listener; });
}

bool MCIceTransport::PortTaken(Kind type, uint16_t port) const
{
    return std::ranges::any_of(_Sockets, [&](const auto& entry)
                               { return entry.second.Type == type && entry.second.Port == port; });
}

MCSocket::Handle MCIceTransport::Open(Kind type, uint16_t port)
{
    if (port == 0)
    {
        do
        {
            port = _NextPort++;

            if (_NextPort == 0)
            {
                _NextPort = FirstEphemeralPort;
            }
        } while (PortTaken(type, port));
    }

    const MCSocket::Handle handle = _NextHandle++;
    Socket& socket = _Sockets[handle];
    socket.Type = type;
    socket.Port = port;
    return handle;
}

MCOptionalRef<MCIceTransport::Socket> MCIceTransport::Find(MCSocket::Handle handle, Kind type)
{
    const auto found = _Sockets.find(handle);
    return found != _Sockets.end() && found->second.Type == type ? MCOptionalRef<Socket>(found->second) : std::nullopt;
}
