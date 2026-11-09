#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct _GUID;

/// <summary>
/// Port-only (Redux HD): MechCommander Online's match relay, as its helper (mco-redux-sidecar, 4.0.6.8) speaks it
/// (SPEC-mco-rooms.md's "The match"). A connection is TLS with the ALPN <see cref="Alpn"/>; on it, frames of a type
/// byte, a big-endian 16-bit length and the payload. Each connection opens with a JSON hello naming its mode and the
/// relay answers "ready": a probe (<see cref="MCMode::Status"/>) learns the relay's epoch for the match's ticket; the
/// host's control connection (<see cref="MCMode::Host"/>, the ticket) hears of each guest the relay pairs with it as an
/// "attach" with a grant, and opens a connection of its own for that guest (<see cref="MCMode::Attach"/>, the grant); a
/// guest has one connection (<see cref="MCMode::Join"/>, the ticket). A pair's connections carry the game's session
/// query and its answer as datagrams and its TCP connection as the stream (opened by the guest, its bytes, its close).
/// </summary>
namespace MCMcoRelay
{
    /// <summary>The TLS application protocol the relay speaks (its answer must be the same).</summary>
    inline constexpr std::string_view Alpn = "mco-redux-stock/2";

    /// <summary>A frame's kind, its first byte.</summary>
    enum class MCFrameType : uint8_t
    {
        /// <summary>A JSON control message.</summary>
        Control = 1,
        /// <summary>One of the game's datagrams (its session query or the answer).</summary>
        Datagram = 2,
        /// <summary>The game's TCP connection opened (the guest's side); empty.</summary>
        StreamOpen = 3,
        /// <summary>Bytes of the game's TCP connection.</summary>
        StreamData = 4,
        /// <summary>The game's TCP connection closed its sending side; empty.</summary>
        StreamClose = 5,
        /// <summary>The connection is alive; empty.</summary>
        KeepAlive = 6,
    };

    /// <summary>A frame's header: its type and its payload's length.</summary>
    inline constexpr size_t HeaderBytes = 3;
    /// <summary>The most a frame of each kind carries (the empty kinds carry nothing).</summary>
    inline constexpr size_t ControlMost = 4096;
    inline constexpr size_t DatagramMost = 2048;
    inline constexpr size_t StreamMost = 16384;

    /// <summary>The relay's timings, as its helper keeps them (ms).</summary>
    inline constexpr int ReplyMs = 10000;
    inline constexpr int WriteMs = 12000;
    inline constexpr int KeepAliveMs = 20000;
    /// <summary>A pair's connection that hears nothing (keep-alives included) for this long has gone.</summary>
    inline constexpr int PairQuietMs = 120000;
    /// <summary>The host's control connection that hears nothing for this long has gone.</summary>
    inline constexpr int ControlQuietMs = 65000;
    /// <summary>How long the host's game is given to take the relay's TCP connection for a guest.</summary>
    inline constexpr int GameConnectMs = 3000;

    /// <summary>Whether a frame of <paramref name="type"/> may carry <paramref name="size"/> bytes.</summary>
    bool SizeAllowed(uint8_t type, size_t size);

    /// <summary>A frame: its header and <paramref name="payload"/> (which <see cref="SizeAllowed"/> must allow).</summary>
    std::vector<uint8_t> Frame(MCFrameType type, std::span<const uint8_t> payload = {});

    /// <summary>A frame read.</summary>
    struct MCFrame
    {
        MCFrameType Type = MCFrameType::KeepAlive;
        std::vector<uint8_t> Payload;
    };

    /// <summary>Frames out of a connection's bytes, as they arrive.</summary>
    class MCFrameReader
    {
    public:
        /// <summary>Takes the bytes that arrived.</summary>
        void Feed(std::span<const uint8_t> bytes);

        /// <summary>The next whole frame, or none until more arrive (or once <see cref="Bad"/>).</summary>
        std::optional<MCFrame> Next();

        /// <summary>A frame of a kind the relay never sends, or a length its kind can't have: the connection is wrong.</summary>
        bool Bad() const;

    private:
        std::vector<uint8_t> _Bytes;
        size_t _Read = 0;
        bool _Bad = false;
    };

    /// <summary>What a connection says it is, its hello's mode.</summary>
    enum class MCMode
    {
        /// <summary>A probe, for the relay's epoch.</summary>
        Status,
        /// <summary>The host's control connection (the ticket).</summary>
        Host,
        /// <summary>The host's connection for one guest (the grant).</summary>
        Attach,
        /// <summary>A guest's connection (the ticket).</summary>
        Join,
    };

    /// <summary>The mode's word in the hello.</summary>
    std::string_view ModeName(MCMode mode);

    /// <summary>
    /// The hello opening a connection: its mode and, for <see cref="MCMode::Host"/> and <see cref="MCMode::Join"/>, the
    /// ticket, for <see cref="MCMode::Attach"/> the grant (a probe carries neither).
    /// </summary>
    std::string Hello(MCMode mode, std::string_view credential = {});

    /// <summary>The relay's answer to a hello, or its word to the host's control connection.</summary>
    struct MCReply
    {
        std::string Status;
        std::string Epoch;
        std::string Grant;
    };

    /// <summary>The status of an answer that takes a connection.</summary>
    inline constexpr std::string_view ReadyStatus = "ready";
    /// <summary>The status telling the host of a guest to open a connection for (its grant).</summary>
    inline constexpr std::string_view AttachStatus = "attach";
    /// <summary>The digits of the relay's epoch and of a grant (hex).</summary>
    inline constexpr size_t EpochDigits = 32;
    inline constexpr size_t GrantDigits = 32;

    /// <summary>A control message's JSON as the relay's answer (its status a string; the rest strings if there).</summary>
    std::expected<MCReply, std::string> ReadReply(std::span<const uint8_t> payload);

    /// <summary>The first word of the game's datagrams and join ("MCG1", as its bytes arrive).</summary>
    inline constexpr std::array<uint8_t, 4> GameMagic{'M', 'C', 'G', '1'};
    /// <summary>The game's session query: the magic, kind 1 and the game's GUID.</summary>
    inline constexpr size_t QueryBytes = 21;
    /// <summary>The smallest answer: the magic, kind 2 and the TCP port it takes joins on (then the session).</summary>
    inline constexpr size_t AnswerLeast = 7;

    /// <summary>Whether <paramref name="datagram"/> is the game's session query, the one datagram a guest sends.</summary>
    bool IsQuery(std::span<const uint8_t> datagram);

    /// <summary>Whether <paramref name="datagram"/> is a session's answer, the one datagram a host sends.</summary>
    bool IsAnswer(std::span<const uint8_t> datagram);

    /// <summary>
    /// Sets the TCP port in a session's answer (which <see cref="IsAnswer"/> takes) to <paramref name="port"/>: a guest's
    /// game joins where its answer says, the guest's own listener, not the host game's port across the relay.
    /// </summary>
    void SetAnswerPort(std::span<uint8_t> answer, uint16_t port);

    /// <summary>The TCP port a session's answer gives.</summary>
    uint16_t AnswerPort(std::span<const uint8_t> answer);

    /// <summary>The session query for the game <paramref name="application"/>, as a joining game sends it.</summary>
    std::array<uint8_t, QueryBytes> Query(const _GUID& application);
}
