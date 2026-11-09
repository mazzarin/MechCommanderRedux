#pragma once

#include "lib/MCOptionalRef.h"
#include "platform/MCSocket.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

/// <summary>
/// Port-only (Redux HD): a TLS client connection over the system's TCP socket (Mbed TLS), for MechCommander Online's
/// match relay (network/MCMcoRelay.h). TLS 1.2 or later, its application protocol asked for and held to. The server's
/// certificates are checked by the computer's own trust (an <see cref="MCTrust"/>: the Mac's Security framework,
/// Windows' certificate chain engine) as its browser would, or, in a lab or a test, against one authority alone.
/// One thread uses a link: it is read when its socket or <see cref="Buffered"/> says so, and written whole.
/// </summary>
class MCTlsLink
{
public:
    /// <summary>The computer's trust in a server's certificates.</summary>
    class MCTrust
    {
    public:
        virtual ~MCTrust() = default;

        /// <summary>
        /// Whether the computer trusts <paramref name="chain"/> (the server's certificates as it sent them, DER, its
        /// own first) for <paramref name="host"/>.
        /// </summary>
        /// <returns>Nothing when trusted; else why not.</returns>
        virtual std::string Check(std::span<const std::span<const uint8_t>> chain, std::string_view host) const = 0;
    };

    /// <summary>What to connect to, and how its certificates are trusted.</summary>
    struct MCSettings
    {
        /// <summary>The server's name (its certificate's), looked up for its address.</summary>
        std::string Host;
        uint16_t Port = 0;
        /// <summary>The application protocol asked for; the server must answer with it.</summary>
        std::string Alpn;
        /// <summary>How long the connection and its handshake may take.</summary>
        int TimeoutMs = 0;
        /// <summary>The one authority trusted (PEM), in a lab or a test; else <see cref="Trust"/>'s.</summary>
        std::string AnchorPem;
        /// <summary>The computer's trust, when there's no <see cref="AnchorPem"/>.</summary>
        MCOptionalRef<const MCTrust> Trust = std::nullopt;
    };

    /// <summary>Connects and completes the handshake, the server's certificates trusted.</summary>
    /// <returns>The link, or what failed (the address, the connection, the handshake, the trust or the protocol).</returns>
    static std::expected<std::unique_ptr<MCTlsLink>, std::string> Connect(const MCSettings& settings);

    ~MCTlsLink();
    MCTlsLink(const MCTlsLink&) = delete;
    MCTlsLink& operator=(const MCTlsLink&) = delete;

    /// <summary>The connection's socket, to wait on for something to read.</summary>
    MCSocket::Handle Socket() const;

    /// <summary>Whether bytes already received wait to be read (the socket may then have nothing new).</summary>
    bool Buffered() const;

    /// <summary>Reads what has arrived, without blocking.</summary>
    /// <returns>The bytes read, 0 when nothing is waiting, -1 when the connection closed or failed.</returns>
    int Read(std::span<uint8_t> data);

    /// <summary>Sends all of <paramref name="data"/>, waiting up to <paramref name="timeoutMs"/> for room.</summary>
    std::expected<void, std::string> Write(std::span<const uint8_t> data, int timeoutMs);

    /// <summary>Says goodbye to the server (TLS's close) and closes the connection; reads and writes then fail.</summary>
    void Close();

private:
    struct MCState;

    explicit MCTlsLink(std::unique_ptr<MCState> state);

    std::unique_ptr<MCState> _State;
};
