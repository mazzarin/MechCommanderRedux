#pragma once

#include "network/MCMcoRelay.h"
#include "platform/MCSocket.h"
#include "platform/MCTlsLink.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

/// <summary>
/// Port-only (Redux HD): a game on this computer carried over MechCommander Online's match relay (network/MCMcoRelay.h),
/// as MCO's helper carries its own game: the game hosts or joins on this computer's loopback as a launcher's game does,
/// and the bridge passes its session query, the answer and its TCP connection over the relay. A guest's bridge listens
/// on 127.0.0.1 for its game (<c>-join 127.0.0.1:port</c>); a host's opens a connection to the relay for each guest the
/// relay pairs with it and passes that guest to the game's port as if it had come there.
/// </summary>
namespace MCMcoBridge
{
    /// <summary>A connection to the relay as the bridge uses it (TLS to MCO's relay; plain TCP in the tests).</summary>
    class MCStream
    {
    public:
        virtual ~MCStream() = default;

        /// <summary>The socket to wait on for something to read.</summary>
        virtual MCSocket::Handle Socket() const = 0;
        /// <summary>Whether bytes already received wait to be read.</summary>
        virtual bool Buffered() const = 0;
        /// <summary>What has arrived (without blocking): the bytes read, 0 for none, -1 once closed or failed.</summary>
        virtual int Read(std::span<uint8_t> data) = 0;
        /// <summary>Sends all of <paramref name="data"/> within <paramref name="timeoutMs"/>.</summary>
        virtual std::expected<void, std::string> Write(std::span<const uint8_t> data, int timeoutMs) = 0;
    };

    /// <summary>Opens a connection to the relay.</summary>
    using MCDial = std::function<std::expected<std::unique_ptr<MCStream>, std::string>()>;

    /// <summary>Connections to MCO's relay: TLS with <paramref name="settings"/> (their ALPN the relay's).</summary>
    MCDial TlsDial(MCTlsLink::MCSettings settings);

    /// <summary>Told what happens (from the bridge's threads, so it must be safe to call from any).</summary>
    using MCNote = std::function<void(std::string_view)>;

    /// <summary>The relay's epoch, from a probe connection (MCO's server writes it into the match's ticket).</summary>
    std::expected<std::string, std::string> Probe(const MCDial& dial);

    /// <summary>What a bridge has carried, as it goes.</summary>
    struct MCCarried
    {
        std::atomic<uint64_t> DatagramsOut = 0;
        std::atomic<uint64_t> DatagramsIn = 0;
        std::atomic<uint64_t> BytesOut = 0;
        std::atomic<uint64_t> BytesIn = 0;
        /// <summary>A game's TCP connection is through to the game at the other end (a guest's, or any of a host's).</summary>
        std::atomic<bool> Through = false;
    };

    /// <summary>A guest's bridge: its game joins it at 127.0.0.1:<see cref="Port"/>.</summary>
    class MCGuest
    {
    public:
        /// <summary>
        /// Listens on 127.0.0.1 (TCP and UDP on one port) and joins the relay with the match's ticket; the relay
        /// answers once the host has taken the guest.
        /// </summary>
        static std::expected<std::unique_ptr<MCGuest>, std::string> Start(const MCDial& dial, std::string_view ticket,
                                                                          MCNote note);

        ~MCGuest();
        MCGuest(const MCGuest&) = delete;
        MCGuest& operator=(const MCGuest&) = delete;

        /// <summary>The port the game joins (<c>-join 127.0.0.1:port</c>).</summary>
        uint16_t Port() const;

        /// <summary>Whether it still carries the game (the relay hasn't closed it, nor <see cref="Stop"/>).</summary>
        bool Running() const;

        /// <summary>Whether the host's game has taken the game's connection.</summary>
        bool Connected() const;

        /// <summary>Why it stopped (empty while it runs, or when stopped).</summary>
        std::string Ended() const;

        const MCCarried& Carried() const;

        /// <summary>Stops carrying and closes everything (waits for its thread).</summary>
        void Stop();

    private:
        struct MCState;

        explicit MCGuest(std::unique_ptr<MCState> state);

        std::unique_ptr<MCState> _State;
    };

    /// <summary>A host's bridge: each guest the relay pairs with it reaches the game's port on this computer.</summary>
    class MCHost
    {
    public:
        /// <summary>
        /// Opens the host's control connection with the match's ticket; each guest it hears of gets a connection of its
        /// own, passed to the game at 127.0.0.1:<paramref name="gamePort"/>.
        /// </summary>
        static std::expected<std::unique_ptr<MCHost>, std::string> Start(const MCDial& dial, std::string_view ticket,
                                                                         uint16_t gamePort, MCNote note);

        ~MCHost();
        MCHost(const MCHost&) = delete;
        MCHost& operator=(const MCHost&) = delete;

        /// <summary>Whether its control connection is open.</summary>
        bool Running() const;

        /// <summary>The guests it carries now.</summary>
        size_t Guests() const;

        /// <summary>Why it stopped (empty while it runs, or when stopped).</summary>
        std::string Ended() const;

        const MCCarried& Carried() const;

        /// <summary>Stops carrying every guest and closes everything (waits for their threads).</summary>
        void Stop();

    private:
        struct MCState;

        explicit MCHost(std::unique_ptr<MCState> state);

        std::unique_ptr<MCState> _State;
    };

    /// <summary>
    /// Whether the game hosting on 127.0.0.1:<paramref name="gamePort"/> answers its session query (as MCO's launcher
    /// waits for its host's game before asking for the ticket), asking every so often for up to
    /// <paramref name="timeoutMs"/>.
    /// </summary>
    bool WaitForHostGame(uint16_t gamePort, int timeoutMs, const std::atomic<bool>& cancel);
}
