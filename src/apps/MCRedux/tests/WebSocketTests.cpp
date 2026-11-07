#include "stdafx.h"
#include "../MCWebSocketOpen.h"
#include "MCTest.h"

#include <array>
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
