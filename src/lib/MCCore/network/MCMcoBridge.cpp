#include "stdafx.h"
#include "network/MCMcoBridge.h"

#include "network/MCMcoApi.h"
#include "network/MCMultiPlayer.h"

#include <array>
#include <chrono>
#include <format>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;
    using MCMcoRelay::MCFrame;
    using MCMcoRelay::MCFrameType;
    using MCMcoRelay::MCMode;

    /// <summary>The longest a bridge's loop waits before looking at its clock and its stop again.</summary>
    constexpr int LoopWaitMs = 250;
    /// <summary>How often a host game is asked whether it's there.</summary>
    constexpr int HostQueryEveryMs = 500;
    /// <summary>Tries for a free port both TCP and UDP can have (as MCO's helper tries).</summary>
    constexpr int PortTries = 5;
    /// <summary>The most guests one host carries at once (MCO's rooms hold six players).</summary>
    constexpr size_t GuestsMost = 8;
    /// <summary>A read from the relay or the game: up to a stream frame's worth.</summary>
    constexpr size_t ReadBytes = MCMcoRelay::StreamMost;

    int MsSince(Clock::time_point then)
    {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - then).count());
    }

    /// <summary>MCO's relay over TLS.</summary>
    class MCTlsStream final : public MCMcoBridge::MCStream
    {
    public:
        explicit MCTlsStream(std::unique_ptr<MCTlsLink> link) : _Link(std::move(link)) {}

        MCSocket::Handle Socket() const override
        {
            return _Link->Socket();
        }

        bool Buffered() const override
        {
            return _Link->Buffered();
        }

        int Read(std::span<uint8_t> data) override
        {
            return _Link->Read(data);
        }

        std::expected<void, std::string> Write(std::span<const uint8_t> data, int timeoutMs) override
        {
            return _Link->Write(data, timeoutMs);
        }

    private:
        std::unique_ptr<MCTlsLink> _Link;
    };

    /// <summary>A connection to the relay with its frames read as they come.</summary>
    class MCRelayLink
    {
    public:
        explicit MCRelayLink(std::unique_ptr<MCMcoBridge::MCStream> stream) : _Stream(std::move(stream)) {}

        MCMcoBridge::MCStream& Stream()
        {
            return *_Stream;
        }

        /// <summary>Sends a frame (its write held to the relay's time).</summary>
        std::expected<void, std::string> Send(MCFrameType type, std::span<const uint8_t> payload = {})
        {
            const std::vector<uint8_t> frame = MCMcoRelay::Frame(type, payload);
            auto sent = _Stream->Write(frame, MCMcoRelay::WriteMs);

            if (sent)
            {
                _LastSent = Clock::now();
            }

            return sent;
        }

        /// <summary>
        /// Reads what has arrived into frames (<see cref="Next"/>), without blocking.
        /// </summary>
        /// <returns>Whether the connection is still open and its frames well formed.</returns>
        bool Pump()
        {
            std::array<uint8_t, ReadBytes> bytes{};

            for (;;)
            {
                const int read = _Stream->Read(bytes);

                if (read < 0)
                {
                    _Closed = true;
                    break;
                }

                if (read == 0)
                {
                    break;
                }

                _Reader.Feed(std::span<const uint8_t>(bytes).first(static_cast<size_t>(read)));
                _LastHeard = Clock::now();
            }

            return !_Closed && !_Reader.Bad();
        }

        std::optional<MCFrame> Next()
        {
            return _Reader.Next();
        }

        bool Closed() const
        {
            return _Closed;
        }

        bool Bad() const
        {
            return _Reader.Bad();
        }

        /// <summary>Waits up to <paramref name="timeoutMs"/> for the next frame.</summary>
        std::expected<MCFrame, std::string> Wait(int timeoutMs)
        {
            const Clock::time_point start = Clock::now();

            for (;;)
            {
                if (std::optional<MCFrame> frame = _Reader.Next())
                {
                    return std::move(*frame);
                }

                // A frame that came before a close is read first: a relay refusing a connection answers, then closes it.
                const bool open = Pump();

                if (std::optional<MCFrame> frame = _Reader.Next())
                {
                    return std::move(*frame);
                }

                if (!open)
                {
                    return std::unexpected(std::string(Bad() ? "the relay sent a frame it never sends"
                                                             : "the relay closed the connection"));
                }

                const int left = timeoutMs - MsSince(start);

                if (left <= 0)
                {
                    return std::unexpected(std::string("the relay didn't answer in time"));
                }

                if (!_Stream->Buffered())
                {
                    std::array<MCSocketTransport::MCWaiting, 1> waiting{
                        MCSocketTransport::MCWaiting{_Stream->Socket()}};
                    MCSocketTransport::Wait(waiting, left);
                }
            }
        }

        /// <summary>A keep-alive when the last frame sent was long enough ago.</summary>
        std::expected<void, std::string> KeepAlive()
        {
            if (MsSince(_LastSent) >= MCMcoRelay::KeepAliveMs)
            {
                return Send(MCFrameType::KeepAlive);
            }

            return {};
        }

        /// <summary>The time since the relay last sent anything.</summary>
        int QuietMs() const
        {
            return MsSince(_LastHeard);
        }

        /// <summary>How long the bridge may wait before its next keep-alive is due.</summary>
        int UntilKeepAliveMs() const
        {
            return std::max(0, MCMcoRelay::KeepAliveMs - MsSince(_LastSent));
        }

    private:
        std::unique_ptr<MCMcoBridge::MCStream> _Stream;
        MCMcoRelay::MCFrameReader _Reader;
        Clock::time_point _LastSent = Clock::now();
        Clock::time_point _LastHeard = Clock::now();
        bool _Closed = false;
    };

    /// <summary>Opens a connection with its hello and has the relay's "ready" (its answer kept).</summary>
    std::expected<std::pair<std::unique_ptr<MCRelayLink>, MCMcoRelay::MCReply>, std::string> Negotiate(
        const MCMcoBridge::MCDial& dial, MCMode mode, std::string_view credential)
    {
        auto stream = dial();

        if (!stream)
        {
            return std::unexpected(stream.error());
        }

        auto link = std::make_unique<MCRelayLink>(std::move(*stream));
        const std::string hello = MCMcoRelay::Hello(mode, credential);

        if (auto sent = link->Send(MCFrameType::Control,
                                   std::span(reinterpret_cast<const uint8_t*>(hello.data()), hello.size()));
            !sent)
        {
            return std::unexpected(sent.error());
        }

        auto frame = link->Wait(MCMcoRelay::ReplyMs);

        if (!frame)
        {
            return std::unexpected(frame.error());
        }

        if (frame->Type != MCFrameType::Control)
        {
            return std::unexpected(std::string("the relay answered with something other than its answer"));
        }

        auto reply = MCMcoRelay::ReadReply(frame->Payload);

        if (!reply)
        {
            return std::unexpected(reply.error());
        }

        if (reply->Status != MCMcoRelay::ReadyStatus)
        {
            return std::unexpected(std::format("the relay refused the {} connection: {}", MCMcoRelay::ModeName(mode),
                                               MCMcoApi::Printable(reply->Status)));
        }

        return std::pair{std::move(link), std::move(*reply)};
    }

    /// <summary>
    /// One pair's connection to the relay and the game's side of it: on a guest's computer the listener its game joins;
    /// on the host's, the game's port. Run on a thread of its own until stopped or the relay ends it.
    /// </summary>
    class MCPair
    {
    public:
        /// <summary>A guest's pair: its game's UDP and TCP on <paramref name="port"/>.</summary>
        static MCPair Guest(std::unique_ptr<MCRelayLink> link, MCSocket::Handle udp, MCSocket::Handle listener,
                            uint16_t port, MCMcoBridge::MCNote note, MCMcoBridge::MCCarried& carried)
        {
            MCPair pair(std::move(link), std::move(note), carried);
            pair._IsGuest = true;
            pair._Udp = udp;
            pair._Listener = listener;
            pair._Port = port;
            return pair;
        }

        /// <summary>A host's pair for one guest: the game at <paramref name="gamePort"/>, its answers to <paramref name="udp"/>.</summary>
        static MCPair Host(std::unique_ptr<MCRelayLink> link, MCSocket::Handle udp, uint16_t gamePort,
                           MCMcoBridge::MCNote note, MCMcoBridge::MCCarried& carried)
        {
            MCPair pair(std::move(link), std::move(note), carried);
            pair._Udp = udp;
            pair._Port = gamePort;
            pair._Game = MCSocket::Address{MCSocket::LoopbackIp, gamePort};
            pair._GameKnown = true;
            return pair;
        }

        MCPair(MCPair&& other) noexcept
            : _Link(std::move(other._Link)), _Note(std::move(other._Note)), _Carried(other._Carried),
              _IsGuest(other._IsGuest), _Udp(std::exchange(other._Udp, MCSocket::InvalidHandle)),
              _Listener(std::exchange(other._Listener, MCSocket::InvalidHandle)),
              _Tcp(std::exchange(other._Tcp, MCSocket::InvalidHandle)), _Port(other._Port), _Game(other._Game),
              _GameKnown(other._GameKnown)
        {
        }

        MCPair(const MCPair&) = delete;
        MCPair& operator=(const MCPair&) = delete;
        MCPair& operator=(MCPair&&) = delete;

        ~MCPair()
        {
            _Sockets.Close(_Tcp);
            _Sockets.Close(_Listener);
            _Sockets.Close(_Udp);
        }

        /// <summary>Carries the game until <paramref name="stop"/> or the relay ends it.</summary>
        /// <returns>Why it ended (empty when stopped).</returns>
        std::string Run(const std::atomic<bool>& stop)
        {
            while (!stop)
            {
                std::array<MCSocketTransport::MCWaiting, 4> waiting{
                    MCSocketTransport::MCWaiting{_Link->Stream().Socket()},
                    MCSocketTransport::MCWaiting{_Udp},
                    MCSocketTransport::MCWaiting{_Tcp == MCSocket::InvalidHandle ? _Listener : MCSocket::InvalidHandle},
                    MCSocketTransport::MCWaiting{_Tcp},
                };
                const int wait = _Link->Stream().Buffered() ? 0 : std::min(LoopWaitMs, _Link->UntilKeepAliveMs());
                MCSocketTransport::Wait(waiting, wait);

                if (std::string ended = FromRelay(); !ended.empty())
                {
                    return ended;
                }

                if (_Link->QuietMs() > MCMcoRelay::PairQuietMs)
                {
                    return "the relay went quiet";
                }

                if (std::string ended = FromGameUdp(); !ended.empty())
                {
                    return ended;
                }

                if (std::string ended = AcceptGame(); !ended.empty())
                {
                    return ended;
                }

                if (std::string ended = FromGameTcp(); !ended.empty())
                {
                    return ended;
                }

                if (auto kept = _Link->KeepAlive(); !kept)
                {
                    return kept.error();
                }
            }

            return {};
        }

    private:
        MCPair(std::unique_ptr<MCRelayLink> link, MCMcoBridge::MCNote note, MCMcoBridge::MCCarried& carried)
            : _Link(std::move(link)), _Note(std::move(note)), _Carried(carried)
        {
        }

        void Note(std::string_view text) const
        {
            if (_Note)
            {
                _Note(text);
            }
        }

        std::string Send(MCFrameType type, std::span<const uint8_t> payload = {})
        {
            auto sent = _Link->Send(type, payload);
            return sent ? std::string() : sent.error();
        }

        /// <summary>The relay's frames, each to the game.</summary>
        std::string FromRelay()
        {
            const bool open = _Link->Pump();

            while (std::optional<MCFrame> frame = _Link->Next())
            {
                if (std::string ended = Handle(*frame); !ended.empty())
                {
                    return ended;
                }
            }

            if (!open)
            {
                return _Link->Bad() ? "the relay sent a frame it never sends" : "the relay closed the connection";
            }

            return {};
        }

        std::string Handle(MCFrame& frame)
        {
            switch (frame.Type)
            {
            case MCFrameType::KeepAlive:
                return {};
            case MCFrameType::Datagram:
                return FromRelayDatagram(frame.Payload);
            case MCFrameType::StreamOpen:
                return FromRelayOpen();
            case MCFrameType::StreamData:
                return FromRelayData(frame.Payload);
            case MCFrameType::StreamClose:
                if (!_PeerOpened)
                {
                    return "the relay closed a connection it never opened";
                }

                CloseGameTcp(false);
                return {};
            case MCFrameType::Control:
                break;
            }

            return "the relay sent a control message mid-match";
        }

        std::string FromRelayDatagram(std::vector<uint8_t>& datagram)
        {
            // Only the game's own datagrams pass: the guest's session query to the host's game, the answer back, the
            // answer's port made the guest's own listener's.
            if (_IsGuest ? !MCMcoRelay::IsAnswer(datagram) : !MCMcoRelay::IsQuery(datagram))
            {
                return {};
            }

            if (!_GameKnown)
            {
                return {};
            }

            if (_IsGuest)
            {
                MCMcoRelay::SetAnswerPort(datagram, _Port);
            }

            _Sockets.SendTo(_Udp, _Game, datagram);
            _Carried.DatagramsIn++;
            return {};
        }

        std::string FromRelayOpen()
        {
            if (_PeerOpened)
            {
                return "the relay opened the game's connection twice";
            }

            _PeerOpened = true;

            if (_IsGuest)
            {
                _Carried.Through = true;
                Note("the host's game took the connection");
                return {};
            }

            // A guest's game connected: so does the host's bridge, to the game's port, and says so.
            _Tcp = _Sockets.ConnectTcp(MCSocket::Address{MCSocket::LoopbackIp, _Port}, MCMcoRelay::GameConnectMs);

            if (_Tcp == MCSocket::InvalidHandle)
            {
                return std::format("the game didn't take a guest's connection on port {}", _Port);
            }

            _Carried.Through = true;
            Note("a guest's game connected");
            return Send(MCFrameType::StreamOpen);
        }

        std::string FromRelayData(std::span<const uint8_t> bytes)
        {
            if (!_PeerOpened)
            {
                return "the relay sent the game's bytes before its connection";
            }

            if (_Tcp == MCSocket::InvalidHandle)
            {
                // The game closed its side: what still comes has nowhere to go.
                return {};
            }

            const Clock::time_point start = Clock::now();
            size_t sent = 0;

            while (sent < bytes.size())
            {
                const int took = _Sockets.Send(_Tcp, bytes.subspan(sent));

                if (took < 0)
                {
                    CloseGameTcp(true);
                    return {};
                }

                sent += static_cast<size_t>(took);

                if (took == 0)
                {
                    const int left = MCMcoRelay::WriteMs - MsSince(start);

                    if (left <= 0)
                    {
                        return "the game stopped taking its connection's bytes";
                    }

                    std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{_Tcp, true}};
                    MCSocketTransport::Wait(waiting, left);
                }
            }

            _Carried.BytesIn += bytes.size();
            return {};
        }

        std::string FromGameUdp()
        {
            std::array<uint8_t, MCMcoRelay::DatagramMost + 1> datagram{};

            for (;;)
            {
                MCSocket::Address from;
                const int size = _Sockets.ReceiveFrom(_Udp, datagram, from);

                if (size <= 0)
                {
                    return {};
                }

                const std::span<const uint8_t> received = std::span<const uint8_t>(datagram).first(static_cast<size_t>(size));

                if (_IsGuest)
                {
                    // The guest's game asks for the session: its address is where the answer goes.
                    if (!MCMcoRelay::IsQuery(received) || !MCSocket::IsLoopback(from.Ip))
                    {
                        continue;
                    }

                    _Game = from;
                    _GameKnown = true;
                }
                else if (from != _Game || !MCMcoRelay::IsAnswer(received))
                {
                    continue;
                }

                if (std::string ended = Send(MCFrameType::Datagram, received); !ended.empty())
                {
                    return ended;
                }

                _Carried.DatagramsOut++;
            }
        }

        std::string AcceptGame()
        {
            if (_Listener == MCSocket::InvalidHandle || _Tcp != MCSocket::InvalidHandle)
            {
                return {};
            }

            MCSocket::Address from;
            const MCSocket::Handle game = _Sockets.AcceptTcp(_Listener, from);

            if (game == MCSocket::InvalidHandle)
            {
                return {};
            }

            if (!MCSocket::IsLoopback(from.Ip))
            {
                _Sockets.Close(game);
                return {};
            }

            // One connection, the game's: the listener closes, as MCO's helper closes its own.
            _Tcp = game;
            _Sockets.Close(_Listener);
            _Listener = MCSocket::InvalidHandle;
            Note("the game connected");
            return Send(MCFrameType::StreamOpen);
        }

        std::string FromGameTcp()
        {
            if (_Tcp == MCSocket::InvalidHandle || (!_IsGuest && !_PeerOpened))
            {
                return {};
            }

            std::array<uint8_t, MCMcoRelay::StreamMost> bytes{};

            for (;;)
            {
                const int read = _Sockets.Receive(_Tcp, bytes);

                if (read == 0)
                {
                    return {};
                }

                if (read < 0)
                {
                    CloseGameTcp(true);
                    return {};
                }

                if (std::string ended =
                        Send(MCFrameType::StreamData, std::span<const uint8_t>(bytes).first(static_cast<size_t>(read)));
                    !ended.empty())
                {
                    return ended;
                }

                _Carried.BytesOut += static_cast<size_t>(read);
            }
        }

        /// <summary>
        /// Closes the game's connection: the game closed it (<paramref name="toldRelay"/>: the relay is told), or the
        /// other end did.
        /// </summary>
        void CloseGameTcp(bool tellRelay)
        {
            if (_Tcp == MCSocket::InvalidHandle)
            {
                return;
            }

            _Sockets.Close(_Tcp);
            _Tcp = MCSocket::InvalidHandle;
            Note(tellRelay ? "the game closed its connection" : "the other game closed the connection");

            if (tellRelay && !_SentClose)
            {
                // A close that can't be sent is the relay's connection failing, which the next read finds.
                _SentClose = true;
                Send(MCFrameType::StreamClose);
            }
        }

        std::unique_ptr<MCRelayLink> _Link;
        MCMcoBridge::MCNote _Note;
        MCMcoBridge::MCCarried& _Carried;
        MCSocketTransport _Sockets;
        bool _IsGuest = false;
        MCSocket::Handle _Udp = MCSocket::InvalidHandle;
        MCSocket::Handle _Listener = MCSocket::InvalidHandle;
        MCSocket::Handle _Tcp = MCSocket::InvalidHandle;
        /// <summary>A guest's: its listener's port; a host's: the game's.</summary>
        uint16_t _Port = 0;
        /// <summary>Where the game's datagrams go (a guest's game once it has asked).</summary>
        MCSocket::Address _Game;
        bool _GameKnown = false;
        bool _PeerOpened = false;
        bool _SentClose = false;
    };

    /// <summary>A thread's word on why it stopped, read from others.</summary>
    class MCEnding
    {
    public:
        void Set(std::string why)
        {
            const std::scoped_lock lock(_Lock);
            _Why = std::move(why);
        }

        std::string Get() const
        {
            const std::scoped_lock lock(_Lock);
            return _Why;
        }

    private:
        mutable std::mutex _Lock;
        std::string _Why;
    };
}

namespace MCMcoBridge
{
    MCDial TlsDial(MCTlsLink::MCSettings settings)
    {
        settings.Alpn = std::string(MCMcoRelay::Alpn);
        return [settings]() -> std::expected<std::unique_ptr<MCStream>, std::string>
        {
            auto link = MCTlsLink::Connect(settings);

            if (!link)
            {
                return std::unexpected(link.error());
            }

            return std::make_unique<MCTlsStream>(std::move(*link));
        };
    }

    std::expected<std::string, std::string> Probe(const MCDial& dial)
    {
        auto negotiated = Negotiate(dial, MCMode::Status, {});

        if (!negotiated)
        {
            return std::unexpected(negotiated.error());
        }

        const std::string& epoch = negotiated->second.Epoch;

        if (!MCMcoApi::IsHex(epoch, MCMcoRelay::EpochDigits))
        {
            return std::unexpected(std::string("the relay's epoch isn't one"));
        }

        return epoch;
    }

    struct MCGuest::MCState
    {
        std::thread Thread;
        std::atomic<bool> Stopping = false;
        std::atomic<bool> Running = true;
        uint16_t Port = 0;
        MCEnding Ending;
        MCCarried Carried;
    };

    MCGuest::MCGuest(std::unique_ptr<MCState> state) : _State(std::move(state)) {}

    std::expected<std::unique_ptr<MCGuest>, std::string> MCGuest::Start(const MCDial& dial, std::string_view ticket,
                                                                        MCNote note)
    {
        MCSocketTransport sockets;
        MCSocket::Handle listener = MCSocket::InvalidHandle;
        MCSocket::Handle udp = MCSocket::InvalidHandle;
        uint16_t port = 0;

        // The game asks and connects on one port: TCP's free one, if UDP can have it too.
        for (int tries = 0; tries < PortTries && udp == MCSocket::InvalidHandle; tries++)
        {
            sockets.Close(listener);
            listener = sockets.ListenTcpOn(MCSocket::LoopbackIp, 0);
            port = listener == MCSocket::InvalidHandle ? 0 : sockets.LocalPort(listener);

            if (port != 0)
            {
                udp = sockets.OpenUdpOn(MCSocket::LoopbackIp, port);
            }
        }

        if (udp == MCSocket::InvalidHandle)
        {
            sockets.Close(listener);
            return std::unexpected(std::string("no port on this computer for the game to join"));
        }

        auto negotiated = Negotiate(dial, MCMode::Join, ticket);

        if (!negotiated)
        {
            sockets.Close(listener);
            sockets.Close(udp);
            return std::unexpected(negotiated.error());
        }

        auto state = std::make_unique<MCState>();
        state->Port = port;
        MCState& running = *state;
        auto pair = std::make_shared<MCPair>(
            MCPair::Guest(std::move(negotiated->first), udp, listener, port, std::move(note), running.Carried));
        state->Thread = std::thread(
            [&running, pair]()
            {
                running.Ending.Set(pair->Run(running.Stopping));
                running.Running = false;
            });
        return std::unique_ptr<MCGuest>(new MCGuest(std::move(state)));
    }

    MCGuest::~MCGuest()
    {
        Stop();
    }

    uint16_t MCGuest::Port() const
    {
        return _State->Port;
    }

    bool MCGuest::Running() const
    {
        return _State->Running;
    }

    bool MCGuest::Connected() const
    {
        return _State->Carried.Through;
    }

    std::string MCGuest::Ended() const
    {
        return _State->Ending.Get();
    }

    const MCCarried& MCGuest::Carried() const
    {
        return _State->Carried;
    }

    void MCGuest::Stop()
    {
        _State->Stopping = true;

        if (_State->Thread.joinable())
        {
            _State->Thread.join();
        }
    }

    struct MCHost::MCState
    {
        struct MCGuestPair
        {
            std::thread Thread;
            std::atomic<bool> Done = false;
        };

        std::thread Thread;
        std::atomic<bool> Stopping = false;
        std::atomic<bool> Running = true;
        std::atomic<size_t> Guests = 0;
        MCEnding Ending;
        MCCarried Carried;
        MCDial Dial;
        MCNote Note;
        uint16_t GamePort = 0;
        std::list<MCGuestPair> Pairs;

        void Tell(std::string_view text) const
        {
            if (Note)
            {
                Note(text);
            }
        }

        /// <summary>A guest the relay paired with the host: its own connection, carried on a thread of its own.</summary>
        void Attach(const std::string& grant)
        {
            std::erase_if(Pairs,
                          [](MCGuestPair& pair)
                          {
                              if (!pair.Done)
                              {
                                  return false;
                              }

                              pair.Thread.join();
                              return true;
                          });

            if (Pairs.size() >= GuestsMost)
            {
                Tell("a guest was turned away: too many at once");
                return;
            }

            MCGuestPair& slot = Pairs.emplace_back();
            slot.Thread = std::thread(
                [this, grant, &slot]()
                {
                    Guests++;
                    std::string ended;
                    auto negotiated = Negotiate(Dial, MCMode::Attach, grant);

                    if (!negotiated)
                    {
                        ended = negotiated.error();
                    }
                    else
                    {
                        MCSocketTransport sockets;
                        const MCSocket::Handle udp = sockets.OpenUdpOn(MCSocket::LoopbackIp, 0);

                        if (udp == MCSocket::InvalidHandle)
                        {
                            ended = "no port on this computer for a guest's datagrams";
                        }
                        else
                        {
                            Tell("a guest joined through the relay");
                            MCPair pair = MCPair::Host(std::move(negotiated->first), udp, GamePort, Note, Carried);
                            ended = pair.Run(Stopping);
                        }
                    }

                    if (!ended.empty())
                    {
                        Tell(std::format("a guest's connection ended: {}", ended));
                    }

                    Guests--;
                    slot.Done = true;
                });
        }

        /// <summary>The control connection: keep-alives, and each guest the relay pairs with the host.</summary>
        std::string Run(MCRelayLink& control)
        {
            while (!Stopping)
            {
                std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{control.Stream().Socket()}};
                MCSocketTransport::Wait(waiting,
                                        control.Stream().Buffered() ? 0 : std::min(LoopWaitMs, control.UntilKeepAliveMs()));
                const bool open = control.Pump();

                while (std::optional<MCFrame> frame = control.Next())
                {
                    if (frame->Type == MCFrameType::KeepAlive)
                    {
                        continue;
                    }

                    if (frame->Type != MCFrameType::Control)
                    {
                        return "the relay sent the control connection something other than a message";
                    }

                    auto word = MCMcoRelay::ReadReply(frame->Payload);

                    if (!word)
                    {
                        return word.error();
                    }

                    if (word->Status != MCMcoRelay::AttachStatus || !MCMcoApi::IsHex(word->Grant, MCMcoRelay::GrantDigits))
                    {
                        return std::format("the relay said {}", MCMcoApi::Printable(word->Status));
                    }

                    Attach(word->Grant);
                }

                if (!open)
                {
                    return control.Bad() ? "the relay sent a frame it never sends" : "the relay closed the host's connection";
                }

                if (control.QuietMs() > MCMcoRelay::ControlQuietMs)
                {
                    return "the relay went quiet";
                }

                if (auto kept = control.KeepAlive(); !kept)
                {
                    return kept.error();
                }
            }

            return {};
        }
    };

    MCHost::MCHost(std::unique_ptr<MCState> state) : _State(std::move(state)) {}

    std::expected<std::unique_ptr<MCHost>, std::string> MCHost::Start(const MCDial& dial, std::string_view ticket,
                                                                      uint16_t gamePort, MCNote note)
    {
        auto negotiated = Negotiate(dial, MCMode::Host, ticket);

        if (!negotiated)
        {
            return std::unexpected(negotiated.error());
        }

        auto state = std::make_unique<MCState>();
        state->Dial = dial;
        state->Note = std::move(note);
        state->GamePort = gamePort;
        MCState& running = *state;
        std::shared_ptr<MCRelayLink> control = std::move(negotiated->first);
        state->Thread = std::thread(
            [&running, control]()
            {
                running.Ending.Set(running.Run(*control));
                running.Running = false;
                // The guests go with the host's connection, as MCO's helper ends with it.
                running.Stopping = true;

                for (MCState::MCGuestPair& pair : running.Pairs)
                {
                    pair.Thread.join();
                }

                running.Pairs.clear();
            });
        return std::unique_ptr<MCHost>(new MCHost(std::move(state)));
    }

    MCHost::~MCHost()
    {
        Stop();
    }

    bool MCHost::Running() const
    {
        return _State->Running;
    }

    size_t MCHost::Guests() const
    {
        return _State->Guests;
    }

    std::string MCHost::Ended() const
    {
        return _State->Ending.Get();
    }

    const MCCarried& MCHost::Carried() const
    {
        return _State->Carried;
    }

    void MCHost::Stop()
    {
        _State->Stopping = true;

        if (_State->Thread.joinable())
        {
            _State->Thread.join();
        }
    }

    bool WaitForHostGame(uint16_t gamePort, int timeoutMs, const std::atomic<bool>& cancel)
    {
        MCSocketTransport sockets;
        const MCSocket::Handle udp = sockets.OpenUdpOn(MCSocket::LoopbackIp, 0);

        if (udp == MCSocket::InvalidHandle)
        {
            return false;
        }

        const std::array<uint8_t, MCMcoRelay::QueryBytes> query = MCMcoRelay::Query(MultiPlayerAppGuid);
        const MCSocket::Address game{MCSocket::LoopbackIp, gamePort};
        const Clock::time_point start = Clock::now();
        Clock::time_point asked = start - std::chrono::milliseconds(HostQueryEveryMs);
        bool answered = false;

        while (!answered && !cancel && MsSince(start) < timeoutMs)
        {
            if (MsSince(asked) >= HostQueryEveryMs)
            {
                sockets.SendTo(udp, game, query);
                asked = Clock::now();
            }

            std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{udp}};
            MCSocketTransport::Wait(waiting, LoopWaitMs);
            std::array<uint8_t, MCMcoRelay::DatagramMost + 1> answer{};
            MCSocket::Address from;

            for (int size = sockets.ReceiveFrom(udp, answer, from); size > 0;
                 size = sockets.ReceiveFrom(udp, answer, from))
            {
                const std::span<const uint8_t> received = std::span<const uint8_t>(answer).first(static_cast<size_t>(size));
                answered = answered ||
                           (from == game && MCMcoRelay::IsAnswer(received) && MCMcoRelay::AnswerPort(received) == gamePort);
            }
        }

        sockets.Close(udp);
        return answered;
    }
}
