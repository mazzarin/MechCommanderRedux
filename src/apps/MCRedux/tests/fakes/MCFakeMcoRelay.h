#pragma once

#include "network/MCMcoBridge.h"
#include "network/MCMcoRelay.h"
#include "platform/MCSocket.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Port-only (Redux HD): a stand-in for MechCommander Online's match relay in the process (network/MCMcoRelay.h, as
// its helper speaks to it), over plain TCP on the loopback rather than TLS. Each connection's hello is answered as the
// relay answers it: a probe gets the epoch, the host's control connection "ready", a guest's join is told to the host's
// control connection as an attach with a grant and answered once the host's connection for that grant has come. Then a
// pair's frames pass from one end to the other, and either end closing closes the other.
namespace MCFakeMcoRelay
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    /// <summary>The epoch the stand-in gives probes, until told another.</summary>
    inline constexpr std::string_view Epoch = "0123456789abcdef0123456789abcdef";
    /// <summary>How long a connection to the stand-in may take, and a write to one of its connections.</summary>
    inline constexpr int ConnectMs = 2000;
    inline constexpr int WriteMs = 2000;
    /// <summary>The longest its loop waits before looking at its stop and its orders again.</summary>
    inline constexpr int LoopMs = 20;
    /// <summary>A read of what a connection sent.</summary>
    inline constexpr size_t ReadBytes = 4096;

    /// <summary>Sends all of <paramref name="data"/> on <paramref name="socket"/>, waiting for room up to <paramref name="timeoutMs"/>.</summary>
    inline std::expected<void, std::string> SendAll(MCSocketTransport& sockets, MCSocket::Handle socket,
                                                    std::span<const uint8_t> data, int timeoutMs)
    {
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        size_t sent = 0;

        while (sent < data.size())
        {
            const int took = sockets.Send(socket, data.subspan(sent));

            if (took < 0)
            {
                return std::unexpected(std::string("the connection failed"));
            }

            sent += static_cast<size_t>(took);

            if (took == 0)
            {
                const auto left =
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();

                if (left <= 0)
                {
                    return std::unexpected(std::string("sending took too long"));
                }

                std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{socket, true}};
                MCSocketTransport::Wait(waiting, static_cast<int>(left));
            }
        }

        return {};
    }

    /// <summary>A plain TCP connection as the bridge's stream to the relay.</summary>
    class MCPlainStream final : public MCMcoBridge::MCStream
    {
    public:
        explicit MCPlainStream(MCSocket::Handle socket) : _Socket(socket) {}
        ~MCPlainStream() override { _Sockets.Close(_Socket); }
        MCPlainStream(const MCPlainStream&) = delete;
        MCPlainStream& operator=(const MCPlainStream&) = delete;

        MCSocket::Handle Socket() const override { return _Socket; }
        bool Buffered() const override { return false; }
        int Read(std::span<uint8_t> data) override { return _Sockets.Receive(_Socket, data); }

        std::expected<void, std::string> Write(std::span<const uint8_t> data, int timeoutMs) override
        {
            return SendAll(_Sockets, _Socket, data, timeoutMs);
        }

    private:
        MCSocketTransport _Sockets;
        MCSocket::Handle _Socket;
    };

    /// <summary>The stand-in relay, listening on 127.0.0.1 from its making until its end.</summary>
    class MCRelay
    {
    public:
        MCRelay()
        {
            _Listener = _Sockets.ListenTcpOn(MCSocket::LoopbackIp, 0);
            _Port = _Listener == MCSocket::InvalidHandle ? 0 : _Sockets.LocalPort(_Listener);
            _Thread = std::thread([this] { Run(); });
        }

        ~MCRelay()
        {
            _Stopping = true;
            _Thread.join();

            for (auto& [id, connection] : _Connections)
            {
                _Sockets.Close(connection.Socket);
            }

            _Sockets.Close(_Listener);
        }

        MCRelay(const MCRelay&) = delete;
        MCRelay& operator=(const MCRelay&) = delete;

        /// <summary>The port it listens on (0 when it couldn't).</summary>
        uint16_t Port() const { return _Port; }

        /// <summary>Connections to the stand-in, as the bridge makes them.</summary>
        MCMcoBridge::MCDial Dial() const
        {
            const uint16_t port = _Port;
            return [port]() -> std::expected<std::unique_ptr<MCMcoBridge::MCStream>, std::string>
            {
                MCSocketTransport sockets;
                const MCSocket::Handle socket =
                    sockets.ConnectTcp(MCSocket::Address{MCSocket::LoopbackIp, port}, ConnectMs);

                if (socket == MCSocket::InvalidHandle)
                {
                    return std::unexpected(std::string("the stand-in relay couldn't be reached"));
                }

                return std::make_unique<MCPlainStream>(socket);
            };
        }

        /// <summary>Every hello answered with <paramref name="status"/> (a refusal) and closed; empty: as the relay answers.</summary>
        void Refuse(std::string status)
        {
            const std::scoped_lock lock(_Lock);
            _Refusal = std::move(status);
        }

        /// <summary>The epoch a probe is given.</summary>
        void SetEpoch(std::string epoch)
        {
            const std::scoped_lock lock(_Lock);
            _Epoch = std::move(epoch);
        }

        /// <summary>The hellos heard, in order, each as its mode and credential ("join ticket").</summary>
        std::vector<std::string> Hellos() const
        {
            const std::scoped_lock lock(_Lock);
            return _Hellos;
        }

        /// <summary>The pairs made.</summary>
        size_t Pairs() const
        {
            const std::scoped_lock lock(_Lock);
            return _PairsMade;
        }

        /// <summary>The frames of <paramref name="type"/> passed from one end of a pair to the other.</summary>
        size_t Passed(MCMcoRelay::MCFrameType type) const
        {
            const std::scoped_lock lock(_Lock);
            const auto it = _Passed.find(type);
            return it == _Passed.end() ? 0 : it->second;
        }

        /// <summary>Sends <paramref name="bytes"/> as they are to every connection whose hello was <paramref name="mode"/>.</summary>
        void SendRaw(std::string_view mode, std::vector<uint8_t> bytes)
        {
            const std::scoped_lock lock(_Lock);
            _Raw.emplace_back(std::string(mode), std::move(bytes));
        }

        /// <summary>Closes every connection whose hello was <paramref name="mode"/> (and their pairs' other ends).</summary>
        void Close(std::string_view mode)
        {
            const std::scoped_lock lock(_Lock);
            _Closing.emplace_back(mode);
        }

    private:
        struct MCConnection
        {
            MCSocket::Handle Socket = MCSocket::InvalidHandle;
            MCMcoRelay::MCFrameReader Reader;
            /// <summary>Its hello's mode (empty until heard).</summary>
            std::string Mode;
            /// <summary>A guest's grant (told to the host), or the grant an attach came with.</summary>
            std::string Grant;
            /// <summary>The other end of its pair (0: none).</summary>
            int Partner = 0;
            bool Closed = false;
        };

        void Run()
        {
            while (!_Stopping)
            {
                std::vector<MCSocketTransport::MCWaiting> waiting{MCSocketTransport::MCWaiting{_Listener}};

                {
                    const std::scoped_lock lock(_Lock);

                    for (const auto& [id, connection] : _Connections)
                    {
                        waiting.push_back(MCSocketTransport::MCWaiting{connection.Socket});
                    }
                }

                MCSocketTransport::Wait(waiting, LoopMs);
                const std::scoped_lock lock(_Lock);
                Accept();

                for (auto& [id, connection] : _Connections)
                {
                    Read(id, connection);
                }

                Orders();
                Drop();
            }
        }

        void Accept()
        {
            for (;;)
            {
                MCSocket::Address from;
                const MCSocket::Handle socket = _Sockets.AcceptTcp(_Listener, from);

                if (socket == MCSocket::InvalidHandle)
                {
                    return;
                }

                _Connections[++_LastId].Socket = socket;
            }
        }

        void Read(int id, MCConnection& connection)
        {
            std::array<uint8_t, ReadBytes> bytes{};
            bool ended = false;

            for (int read = _Sockets.Receive(connection.Socket, bytes); read != 0 && !ended;
                 read = _Sockets.Receive(connection.Socket, bytes))
            {
                ended = read < 0;

                if (!ended)
                {
                    connection.Reader.Feed(std::span<const uint8_t>(bytes).first(static_cast<size_t>(read)));
                }
            }

            // What came before a close still passes on.
            while (!connection.Closed)
            {
                std::optional<MCMcoRelay::MCFrame> frame = connection.Reader.Next();

                if (!frame)
                {
                    break;
                }

                if (connection.Mode.empty())
                {
                    Hello(id, connection, *frame);
                }
                else
                {
                    Pass(connection, *frame);
                }
            }

            connection.Closed = connection.Closed || ended || connection.Reader.Bad();
        }

        void Answer(MCConnection& connection, const Json& answer)
        {
            const std::string text = answer.dump();
            const std::vector<uint8_t> frame =
                MCMcoRelay::Frame(MCMcoRelay::MCFrameType::Control,
                                  std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
            connection.Closed = connection.Closed || !SendAll(_Sockets, connection.Socket, frame, WriteMs);
        }

        void Refuse(MCConnection& connection, std::string_view status)
        {
            Answer(connection, Json{{"status", std::string(status)}});
            connection.Closed = true;
        }

        void Hello(int id, MCConnection& connection, const MCMcoRelay::MCFrame& frame)
        {
            const Json hello = Json::parse(frame.Payload.begin(), frame.Payload.end(), nullptr, false);

            if (frame.Type != MCMcoRelay::MCFrameType::Control || !hello.is_object() || !hello.contains("mode") ||
                !hello["mode"].is_string())
            {
                connection.Closed = true;
                return;
            }

            connection.Mode = hello["mode"].get<std::string>();
            const std::string credential = hello.value("ticket", hello.value("grant", std::string()));
            _Hellos.push_back(std::format("{} {}", connection.Mode, credential));

            if (!_Refusal.empty())
            {
                Refuse(connection, _Refusal);
            }
            else if (connection.Mode == "status")
            {
                Answer(connection, Json{{"epoch", _Epoch}, {"status", "ready"}});
            }
            else if (connection.Mode == "host")
            {
                _Host = id;
                Answer(connection, Json{{"status", "ready"}});
            }
            else if (connection.Mode == "join")
            {
                Join(connection);
            }
            else if (connection.Mode == "attach")
            {
                Attach(id, connection, credential);
            }
            else
            {
                Refuse(connection, "unknown mode");
            }
        }

        /// <summary>A guest: the host is told of it, and it waits for the host's connection for it.</summary>
        void Join(MCConnection& guest)
        {
            const auto host = _Connections.find(_Host);

            if (host == _Connections.end() || host->second.Closed)
            {
                Refuse(guest, "no host");
                return;
            }

            guest.Grant = std::format("{:032x}", ++_GrantsMade);
            Answer(host->second, Json{{"grant", guest.Grant}, {"status", "attach"}});
        }

        void Attach(int id, MCConnection& attach, const std::string& grant)
        {
            for (auto& [guestId, guest] : _Connections)
            {
                if (guest.Mode == "join" && guest.Partner == 0 && !guest.Closed && guest.Grant == grant)
                {
                    attach.Grant = grant;
                    attach.Partner = guestId;
                    guest.Partner = id;
                    _PairsMade++;
                    Answer(attach, Json{{"status", "ready"}});
                    Answer(guest, Json{{"status", "ready"}});
                    return;
                }
            }

            Refuse(attach, "unknown grant");
        }

        /// <summary>A pair's frame to its other end (keep-alives and control messages stop here).</summary>
        void Pass(MCConnection& from, const MCMcoRelay::MCFrame& frame)
        {
            if (frame.Type == MCMcoRelay::MCFrameType::KeepAlive || frame.Type == MCMcoRelay::MCFrameType::Control)
            {
                return;
            }

            const auto to = _Connections.find(from.Partner);

            if (to == _Connections.end() || to->second.Closed)
            {
                return;
            }

            _Passed[frame.Type]++;
            const std::vector<uint8_t> bytes = MCMcoRelay::Frame(frame.Type, frame.Payload);
            to->second.Closed = !SendAll(_Sockets, to->second.Socket, bytes, WriteMs);
        }

        /// <summary>The tests' orders: raw bytes to send, connections to close.</summary>
        void Orders()
        {
            for (const auto& [mode, bytes] : _Raw)
            {
                for (auto& [id, connection] : _Connections)
                {
                    if (connection.Mode == mode && !connection.Closed)
                    {
                        connection.Closed = !SendAll(_Sockets, connection.Socket, bytes, WriteMs);
                    }
                }
            }

            _Raw.clear();

            for (const std::string& mode : _Closing)
            {
                for (auto& [id, connection] : _Connections)
                {
                    connection.Closed = connection.Closed || connection.Mode == mode;
                }
            }

            _Closing.clear();
        }

        /// <summary>The closed connections dropped, and their pairs' other ends with them.</summary>
        void Drop()
        {
            for (auto& [id, connection] : _Connections)
            {
                if (connection.Closed && connection.Partner != 0)
                {
                    if (const auto other = _Connections.find(connection.Partner); other != _Connections.end())
                    {
                        other->second.Closed = true;
                    }
                }
            }

            std::erase_if(_Connections,
                          [this](auto& entry)
                          {
                              if (!entry.second.Closed)
                              {
                                  return false;
                              }

                              _Sockets.Close(entry.second.Socket);
                              return true;
                          });
        }

        MCSocketTransport _Sockets;
        MCSocket::Handle _Listener = MCSocket::InvalidHandle;
        uint16_t _Port = 0;
        std::atomic<bool> _Stopping = false;
        mutable std::mutex _Lock;
        std::map<int, MCConnection> _Connections;
        int _LastId = 0;
        int _Host = 0;
        size_t _GrantsMade = 0;
        size_t _PairsMade = 0;
        std::string _Refusal;
        std::string _Epoch{MCFakeMcoRelay::Epoch};
        std::vector<std::string> _Hellos;
        std::map<MCMcoRelay::MCFrameType, size_t> _Passed;
        std::vector<std::pair<std::string, std::vector<uint8_t>>> _Raw;
        std::vector<std::string> _Closing;
        std::thread _Thread;
    };

    /// <summary>A session's answer as the game writes it: the magic, kind 2, its port (little-endian), the session.</summary>
    inline std::vector<uint8_t> SessionAnswer(uint16_t port, std::string_view session)
    {
        std::vector<uint8_t> answer(MCMcoRelay::GameMagic.begin(), MCMcoRelay::GameMagic.end());
        answer.push_back(2);
        answer.push_back(static_cast<uint8_t>(port & 0xff));
        answer.push_back(static_cast<uint8_t>(port >> 8));
        answer.insert(answer.end(), session.begin(), session.end());
        return answer;
    }

    /// <summary>A host game's session answers on 127.0.0.1, each naming <paramref name="named"/> as its port.</summary>
    class MCAnsweringGame
    {
    public:
        explicit MCAnsweringGame(std::optional<uint16_t> named = std::nullopt)
        {
            _Udp = _Sockets.OpenUdpOn(MCSocket::LoopbackIp, 0);
            _Port = _Sockets.LocalPort(_Udp);
            _Named = named.value_or(_Port);
            _Thread = std::thread(
                [this]
                {
                    while (!_Stopping)
                    {
                        std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{_Udp}};
                        MCSocketTransport::Wait(waiting, LoopMs);
                        std::array<uint8_t, MCMcoRelay::DatagramMost> datagram{};
                        MCSocket::Address from;

                        for (int size = _Sockets.ReceiveFrom(_Udp, datagram, from); size > 0;
                             size = _Sockets.ReceiveFrom(_Udp, datagram, from))
                        {
                            if (MCMcoRelay::IsQuery(std::span(datagram).first(static_cast<size_t>(size))))
                            {
                                _Sockets.SendTo(_Udp, from, SessionAnswer(_Named, "Solaris"));
                            }
                        }
                    }
                });
        }

        ~MCAnsweringGame()
        {
            _Stopping = true;
            _Thread.join();
            _Sockets.Close(_Udp);
        }

        MCAnsweringGame(const MCAnsweringGame&) = delete;
        MCAnsweringGame& operator=(const MCAnsweringGame&) = delete;

        uint16_t Port() const { return _Port; }

    private:
        MCSocketTransport _Sockets;
        MCSocket::Handle _Udp = MCSocket::InvalidHandle;
        uint16_t _Port = 0;
        uint16_t _Named = 0;
        std::atomic<bool> _Stopping = false;
        std::thread _Thread;
    };
}
