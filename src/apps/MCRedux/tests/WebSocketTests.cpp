#include "stdafx.h"
#include "../MCWebSocketOpen.h"
#include "MCTest.h"
#include "lib/MCThreadKeeper.h"
#include "platform/MCSocket.h"

#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Port-only (the personal build): this platform's own WebSocket (MCWebSocketMac.mm, MCWebSocketWin.cpp), which the
// internet games reach their service through because it checks the service's certificate against the system's roots
// (libdatachannel's checks none on Windows). With --websocket-service <wss address> (the fork's Windows build passes
// the games service's list), it opens that service, and refuses hosts whose certificates are bad: badssl.com's, kept
// broken on purpose by its maintainers, each a TLS server that a socket must leave before any WebSocket is asked for.
// And, on this machine's loopback, a service whose network stalls: destroying a socket whose close can't go returns at
// once, its close finished on its own thread (Windows'), which the thread keeper joins.

namespace
{
    /// <summary>The option naming a service to open (a <c>wss://</c> address a WebSocket is answered at).</summary>
    constexpr std::string_view ServiceOption = "websocket-service";
    /// <summary>Hosts whose certificate a system that checks refuses: expired, for another name, signed by itself, from a
    /// root no system holds.</summary>
    constexpr std::array BadCertificateHosts = {
        std::string_view("wss://expired.badssl.com/"),
        std::string_view("wss://wrong.host.badssl.com/"),
        std::string_view("wss://self-signed.badssl.com/"),
        std::string_view("wss://untrusted-root.badssl.com/"),
    };

    /// <summary>How long a socket may take to open or close: past WinHTTP's 90 s a step and NSURLSession's 60.</summary>
    constexpr std::chrono::seconds Deadline{100};
    constexpr std::chrono::milliseconds PollStep{50};

    /// <summary>What a socket did before <see cref="Deadline"/>: whether it opened, its first message, why it closed.</summary>
    struct Outcome
    {
        bool Opened = false;
        std::string FirstMessage;
        std::string WhyClosed;
    };

    /// <summary>
    /// A WebSocket service on this machine that answers a socket's handshake, then reads nothing more, as a service
    /// whose network stalled: what the socket sends it fills the connection's buffers, and the rest waits to go.
    /// </summary>
    class MCStalledService
    {
    public:
        MCStalledService()
        {
            REQUIRE(_Net.Startup());
            _Listener = _Net.ListenTcp(0);
            REQUIRE(_Listener != MCSocket::InvalidHandle);
        }

        ~MCStalledService()
        {
            _Net.Close(_Connection);
            _Net.Close(_Listener);
        }

        MCStalledService(const MCStalledService&) = delete;
        MCStalledService& operator=(const MCStalledService&) = delete;

        std::string Url() { return std::format("ws://127.0.0.1:{}/stalled", _Net.LocalPort(_Listener)); }

        /// <summary>Moves the handshake on without waiting: the connection taken, its request read, answered.</summary>
        /// <returns>Whether the handshake is answered.</returns>
        bool Answer()
        {
            if (_Answered)
            {
                return true;
            }

            if (_Connection == MCSocket::InvalidHandle)
            {
                MCSocket::Address from;
                _Connection = _Net.AcceptTcp(_Listener, from);
                return false;
            }

            std::array<uint8_t, ReadBytes> buffer{};
            const int got = _Net.Receive(_Connection, buffer);

            if (got > 0)
            {
                _Request.append(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(got));
            }

            if (_Request.find(HeadEnd) == std::string::npos)
            {
                return false;
            }

            const std::string answer = std::format("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                                   "Connection: Upgrade\r\nSec-WebSocket-Accept: {}\r\n\r\n",
                                                   AcceptFor(KeyIn(_Request)));
            const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(answer.data()), answer.size());
            REQUIRE(_Net.Send(_Connection, bytes) == static_cast<int>(bytes.size()));
            _Answered = true;
            return true;
        }

    private:
        static constexpr size_t ReadBytes = 4096;
        static constexpr std::string_view HeadEnd = "\r\n\r\n";
        static constexpr std::string_view KeyField = "sec-websocket-key:";
        /// <summary>RFC 6455's: the key and this, hashed (SHA-1) and in base64, is the answer's accept.</summary>
        static constexpr std::string_view AcceptGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        static constexpr size_t Sha1Bytes = 20;
        static constexpr size_t Base64Bytes = 64;

        /// <summary>The request's Sec-WebSocket-Key, its field's name in any case.</summary>
        static std::string KeyIn(const std::string& request)
        {
            std::string lower = request;
            std::ranges::transform(lower, lower.begin(), [](char c)
                                   { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            const size_t field = lower.find(KeyField);
            REQUIRE(field != std::string::npos);
            const size_t start = request.find_first_not_of(' ', field + KeyField.size());
            return request.substr(start, request.find("\r\n", start) - start);
        }

        static std::string AcceptFor(const std::string& key)
        {
            const std::string keyed = key + std::string(AcceptGuid);
            std::array<unsigned char, Sha1Bytes> hash{};
            REQUIRE(mbedtls_sha1(reinterpret_cast<const unsigned char*>(keyed.data()), keyed.size(), hash.data()) == 0);
            std::array<unsigned char, Base64Bytes> text{};
            size_t length = 0;
            REQUIRE(mbedtls_base64_encode(text.data(), text.size(), &length, hash.data(), hash.size()) == 0);
            return std::string(reinterpret_cast<const char*>(text.data()), length);
        }

        MCSocketTransport _Net;
        MCSocket::Handle _Listener = MCSocket::InvalidHandle;
        MCSocket::Handle _Connection = MCSocket::InvalidHandle;
        std::string _Request;
        bool _Answered = false;
    };

    /// <summary>Opens <paramref name="url"/> and waits until a message arrives, it closes, or the deadline.</summary>
    Outcome Try(std::string_view url)
    {
        const std::unique_ptr<MCWebSocket> socket = MCOpenWebSocket(std::string(url));
        Outcome outcome;

        for (const auto until = std::chrono::steady_clock::now() + Deadline; std::chrono::steady_clock::now() < until;
             std::this_thread::sleep_for(PollStep))
        {
            const MCWebSocket::State state = socket->GetState();
            outcome.Opened = outcome.Opened || state == MCWebSocket::State::Open;

            std::vector<MCWebSocket::Message> messages = socket->Take();

            if (!messages.empty())
            {
                outcome.Opened = true;
                outcome.FirstMessage = std::move(messages.front().Data);
                socket->Close();
                return outcome;
            }

            if (state == MCWebSocket::State::Closed)
            {
                outcome.WhyClosed = socket->WhyClosed();
                return outcome;
            }
        }

        outcome.WhyClosed = "still waiting at the deadline";
        socket->Close();
        return outcome;
    }
}

TEST_CASE("tool: --websocket-service <address> checks this platform's WebSocket opens the service, and refuses hosts "
          "whose certificates are bad")
{
    const auto service = MCTest::Option(ServiceOption);

    if (!service.has_value())
    {
        return;
    }

    // The service: open, and its first message (the games service's list says its games).
    const Outcome good = Try(*service);
    const MCTest::Scope goodScope(std::format("the service: opened {}, first message \"{}\", closed \"{}\"",
                                              good.Opened, good.FirstMessage, good.WhyClosed));
    CHECK(good.Opened);
    CHECK(!good.FirstMessage.empty());

    for (const std::string_view host : BadCertificateHosts)
    {
        const Outcome bad = Try(host);
        const MCTest::Scope badScope(std::format("{}: opened {}, closed \"{}\"", host, bad.Opened, bad.WhyClosed));
        CHECK(!bad.Opened);
        CHECK(bad.FirstMessage.empty());
        CHECK(bad.WhyClosed.starts_with(MCWebSocketUntrusted));
    }
}

TEST_CASE("websocket: destroying a socket whose close can't go (its network stalled) returns at once, the close "
          "finishing off the game's thread")
{
    constexpr auto OpenPatience = std::chrono::seconds(10);
    // Far under the second Windows' close waits for a stalled network (its LeaveWait).
    constexpr auto AtOnce = std::chrono::milliseconds(250);
    // For its first sends to fill the connection's buffers.
    constexpr auto Filling = std::chrono::milliseconds(200);
    // More than the loopback's buffers hold, less than the most a socket lets wait to go (8 MB).
    constexpr size_t Messages = 7;
    constexpr size_t MessageBytes = size_t{1} << 20;
    MCStalledService service;
    std::unique_ptr<MCWebSocket> socket = MCOpenWebSocket(service.Url());
    REQUIRE(socket != nullptr);

    // Its handshake answered: it opens.
    for (const auto until = std::chrono::steady_clock::now() + OpenPatience;
         socket->GetState() == MCWebSocket::State::Opening && std::chrono::steady_clock::now() < until;
         std::this_thread::sleep_for(PollStep / 10))
    {
        service.Answer();
    }

    const MCTest::Scope scope(std::format("closed \"{}\"", socket->WhyClosed()));
    REQUIRE(socket->GetState() == MCWebSocket::State::Open);

    // More than the connection holds, sent to a service that reads none of it: the rest waits to go.
    const std::vector<uint8_t> message(MessageBytes, 0x5a);

    for (size_t i = 0; i < Messages; i++)
    {
        socket->SendBinary(message);
    }

    std::this_thread::sleep_for(Filling);
    CHECK(socket->Unsent() > 0);
    CHECK(socket->GetState() == MCWebSocket::State::Open);

    // Closed: its close can't go after what waits. (Windows' tries for a second; the Mac's session cancels.)
    socket->Close();
    CHECK(socket->GetState() == MCWebSocket::State::Closed);
#ifdef _WIN32
    CHECK(!socket->Finished());
#endif
    const auto before = std::chrono::steady_clock::now();
    socket.reset();
    CHECK(std::chrono::steady_clock::now() - before < AtOnce);

    // Its thread finishes the close on its own, and the keeper joins it.
    MCThreadKeeper::Process().JoinAll();
    CHECK_EQ(MCThreadKeeper::Process().Kept(), size_t{0});
}
