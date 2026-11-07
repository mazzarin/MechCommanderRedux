#pragma once

// Port: one end of an internet game's link between two games (SPEC-in-game-lobby.md's "How players connect"): a WebRTC
// data channel (libdatachannel), reliable and in order as the TCP link it stands for, encrypted (DTLS), over the UDP
// path ICE finds (libjuice): direct through the players' routers (IPv6 first), else through a relay (TURN).
//
// The two ends never reach each other until ICE has a path: they first trade signals through the service (this end's
// description, then each address it may be reached at, its candidates), which TakeSignals hands out and Heard takes
// in. Everything else is polled from the game's thread, as MCSocket is: libdatachannel's own threads only fill this
// end's queues. This header holds only standard types, so code that includes it never meets the system's socket headers
// (libdatachannel's), which the game's Win32 stand-ins can't.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

/// <summary>One end of a link to another game over ICE (see the file's comment).</summary>
class MCIceLink
{
public:
    /// <summary>Where a link finds its paths: none listed, only the machines' own addresses (a LAN's).</summary>
    struct Settings
    {
        /// <summary>A relay (TURN, over UDP), with the short-lived credentials the service hands out.</summary>
        struct Relay
        {
            std::string Host;
            uint16_t Port = 0;
            std::string User;
            std::string Password;
        };

        /// <summary>STUN servers ("host:port"), which tell a game its address as the internet sees it.</summary>
        std::vector<std::string> Stun;
        std::vector<Relay> Relays;
        /// <summary>Only the relay's path, so neither game learns the other's address (a public game's).</summary>
        bool RelayOnly = false;
        /// <summary>
        /// The one address of this machine the link uses, its only candidate (none: every address). The tests' loopback:
        /// a firewall that holds packets from the machine's other addresses until the player answers it (the Mac's, for
        /// a program not signed by a developer, once it has listened) never holds the loopback's.
        /// </summary>
        std::string BindAddress;
    };

    /// <summary>What one end tells the other through the service, in the order it says them.</summary>
    struct Signal
    {
        /// <summary>"offer" or "answer" (Text its description), or "candidate" (Text one address, Mid its part).</summary>
        std::string Kind;
        std::string Text;
        std::string Mid;

        friend bool operator==(const Signal&, const Signal&) = default;
    };

    enum class State
    {
        /// <summary>Trading signals, or ICE trying the paths.</summary>
        Connecting,
        /// <summary>The channel is open: bytes go both ways.</summary>
        Open,
        /// <summary>No path worked, or the link broke (the other end gone, a bad signal).</summary>
        Failed,
        /// <summary>Either end closed it.</summary>
        Closed,
    };

    /// <summary>The path an open link took, for the reports: never its addresses.</summary>
    enum class Path
    {
        /// <summary>Not open yet, or not known.</summary>
        None,
        DirectIpv6,
        DirectIpv4,
        Relay,
    };

    /// <summary>
    /// The end that opens the link (a guest): it makes the offer, its first signal. None if libdatachannel refused to
    /// start one (logged).
    /// </summary>
    static std::unique_ptr<MCIceLink> Offer(const Settings& settings);

    /// <summary>The end that answers (the host): it answers the offer it hears. None as for Offer.</summary>
    static std::unique_ptr<MCIceLink> Answer(const Settings& settings);

    ~MCIceLink();
    MCIceLink(const MCIceLink&) = delete;
    MCIceLink& operator=(const MCIceLink&) = delete;

    /// <summary>This end's signals since the last call, in order, for the service to pass to the other end.</summary>
    std::vector<Signal> TakeSignals();

    /// <summary>
    /// One of the other end's signals, in the order it said them. One that doesn't parse, or comes out of turn (an
    /// offer to the offering end), fails the link: it came over the network.
    /// </summary>
    void Heard(const Signal& signal);

    State GetState() const;

    /// <summary>The path the open link took (None until it is open).</summary>
    Path GetPath() const;

    /// <summary>Sends <paramref name="bytes"/> after everything sent before; false unless the link is open.</summary>
    bool Send(std::span<const uint8_t> bytes);

    /// <summary>
    /// Moves into <paramref name="into"/> as many of the bytes received as fit, in the order they were sent.
    /// </summary>
    /// <returns>How many: 0 when none are waiting.</returns>
    size_t Receive(std::span<uint8_t> into);

    /// <summary>Closes the link; the other end sees it Closed.</summary>
    void Close();

private:
    struct Impl;
    explicit MCIceLink(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> _Impl;
};
