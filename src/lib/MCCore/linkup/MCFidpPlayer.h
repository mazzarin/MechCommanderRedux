#pragma once

// Original source: mcx\linkup\dpplayer.cpp.

#include "linkup/MCLinkupMessages.h"

class MCFidpMessage;

/// <summary>
/// A player of the linkup session as the SessionManager tracks it: its DirectPlay id and names, the guaranteed
/// messages sent to it and not yet verified, the guaranteed messages received from it out of order, its latency
/// history and the groups it belongs to.
/// </summary>
/// <remarks>
/// Guaranteed messages are numbered per sender with a byte counter (wrapping at 256): <see cref="OutgoingSendCount"/>
/// numbers the messages sent to this player, <see cref="IncomingMessages"/> is indexed by the number of those received
/// from it. The lists share the messages with the SessionManager's queues.
/// </remarks>
class MCFidpPlayer
{
public:
    /// <summary>
    /// The player <paramref name="id"/> named by <paramref name="name"/> (the short name cut to 127 characters, the long
    /// one to 255), with DirectPlay's player <paramref name="flags"/>.
    /// </summary>
    MCFidpPlayer(uint32_t id, const DPNAME& name, uint32_t flags);

    MCFidpPlayer(const MCFidpPlayer&) = delete;
    MCFidpPlayer& operator=(const MCFidpPlayer&) = delete;

    /// <summary>
    /// Queues a guaranteed message sent to this player until it is verified, stamping its send time (and its first
    /// send time, if it was never resent).
    /// </summary>
    void AddToVerifyList(std::shared_ptr<MCFidpMessage> msg);

    /// <summary>
    /// Removes the message numbered <paramref name="sendCount"/> from the verify list (it was acknowledged) and, if it
    /// was sent only once, records its round trip in the latency history.
    /// </summary>
    /// <returns>The message, or null when none has that number.</returns>
    std::shared_ptr<MCFidpMessage> RemoveFromVerifyList(uint8_t sendCount);

    /// <summary>The mean of the non-zero entries of the latency history, 500 ms when there are none.</summary>
    int32_t AverageLatency();

    /// <summary>Whether 128 or more messages to this player are waiting to be verified.</summary>
    bool IsVerifyListFull();

    /// <summary>
    /// How far the send counter has run ahead of the oldest unverified message (modulo 256), 0 when none waits.
    /// </summary>
    int VerifyCountDifference();

    /// <summary>
    /// Stores a guaranteed message received from this player under its number <paramref name="sendCount"/>, unless it
    /// is outside the 128-message window or a duplicate.
    /// </summary>
    /// <returns>Whether it was stored.</returns>
    bool HandleIncomingMessage(std::shared_ptr<MCFidpMessage> msg, int sendCount);

    /// <summary>Advances the next expected incoming number past every message already held.</summary>
    void SetNextIncomingSendCount();

    /// <summary>Takes the next in-order incoming message, if it has arrived.</summary>
    /// <returns>The message, or null.</returns>
    std::shared_ptr<MCFidpMessage> NextMessageToProcess();

    /// <summary>Records that the player joined group <paramref name="groupID"/>.</summary>
    void JoinGroup(uint32_t groupID);

    /// <summary>Records that the player left group <paramref name="groupID"/> (the first record of it).</summary>
    void LeaveGroup(uint32_t groupID);

    /// <summary>Whether the player belongs to group <paramref name="groupID"/>.</summary>
    bool IsInGroup(uint32_t groupID) const;

    /// <summary>The number given to a guaranteed message sent to this player: its slot of the send counters.</summary>
    uint8_t SendCountOf(const MCFidpMessage& msg) const;

    /// <summary>
    /// The size of the round-trip history <see cref="AverageLatency"/> averages; a game rule of the server switch.
    /// </summary>
    static constexpr int32_t LatencyHistory = 5;
    /// <summary>The numbers a guaranteed message can have (a byte on the wire).</summary>
    static constexpr int32_t SendCounts = 256;

    /// <summary>The short name.</summary>
    std::string Name;
    /// <summary>The long name.</summary>
    std::string LongName;
    /// <summary>The player's DPID.</summary>
    uint32_t Id = 0;
    /// <summary>DirectPlay's player flags.</summary>
    uint32_t Flags = 0;
    /// <summary>Guaranteed messages sent to this player, waiting to be verified, oldest first.</summary>
    std::vector<std::shared_ptr<MCFidpMessage>> VerifyList;
    /// <summary>Guaranteed messages received from this player, by their number, until processed in order.</summary>
    std::array<std::shared_ptr<MCFidpMessage>, SendCounts> IncomingMessages{};
    /// <summary>The next slot of <see cref="Latencies"/> to write.</summary>
    int32_t LatencyIndex = 0;
    /// <summary>The last five measured round trips, in ms.</summary>
    std::array<int32_t, LatencyHistory> Latencies{};
    /// <summary>The last <see cref="AverageLatency"/>.</summary>
    int32_t LastAverageLatency = 0;
    /// <summary>Guards the verify list and the latency history.</summary>
    std::recursive_mutex CriticalSection;
    /// <summary>The number given to the last guaranteed message sent to this player (starts at 0xff).</summary>
    uint8_t OutgoingSendCount = 0xff;
    /// <summary>The number of the next incoming message to process.</summary>
    uint8_t NextIncomingToProcess = 0;
    /// <summary>The lowest incoming number not yet received.</summary>
    uint8_t NextIncomingSendCount = 0;
    /// <summary>How many incoming messages are held in <see cref="IncomingMessages"/>.</summary>
    int32_t NumIncomingMessages = 0;
    /// <summary>
    /// The player's number in the session (0-5, -1 until the server gives one): its slot in every message's
    /// <see cref="MCMessageTagger"/>.
    /// </summary>
    int32_t PlayerNumber = -1;
    /// <summary>The last measured round trip, in ms.</summary>
    uint32_t LastLatency = 0;
    /// <summary>The latency the player reported in its last latency message (1000 when it reported 0).</summary>
    uint32_t ReportedLatency = 0;
    /// <summary>Whether the player has a number (guaranteed messages are only resent to such players).</summary>
    bool HasPlayerNumber = true;
    /// <summary>
    /// Port-only (the personal build): on the host, whether this guest has pinged it. A guest pings from its room's
    /// screen (the session screen as it opens; an older build's ready room), so its game is up to be shown the room.
    /// </summary>
    bool HeardPing = false;
    /// <summary>Base delay before an unverified message is resent, in ms.</summary>
    uint32_t ResendDelay = 1500;
    /// <summary>The player's physical memory (from its FISystemInfoMessage).</summary>
    uint32_t TotalPhysicalMemory = 0;
    /// <summary>The ids of the groups the player is in.</summary>
    std::vector<uint32_t> Groups;
};
