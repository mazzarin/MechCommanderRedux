#pragma once

// Port-only (Redux HD; SPEC-mco-hosting.md): an internet game this game hosts from INTERNET is an MCO room too
// (network/MCMcoRooms' HostBoth), its MCO guests coming in over MCO's relay as links of the game's own. The host's room
// carries the mission as its description, and counts the players who have no seat in it: each Redux HD guest is told
// the room (MCMPMessageType::McoRoom), and one whose build has seats asks for one at once (McoSeat), sits in it quietly
// (Sit) and tells the host how MCO answered. A guest that never answers is an earlier build's, with no seat. Nothing
// of it shows on the screens: a room or a seat MCO refuses is only logged, and the game plays on our service alone.

#include "network/MCMcoApi.h"
#include "network/MCMcoRooms.h"
#include "network/MCMultiPlayerMessages.h"
#include "platform/MCServiceSignaling.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MCMcoSeats
{
    /// <summary>
    /// Each frame in the menus and at the results (beside MCRoomEnd::Frame). A host: its internet game hosted on MCO too
    /// once its code is known (INTERNET's game, never a launcher's), the room's description and seats kept, its guests
    /// told the room. A guest: tells its host its seat as MCO answers it.
    /// </summary>
    void Frame();

    /// <summary>
    /// A host's room as our service's list shows it (MCSessionManager::TendInternet): the MCO room's id, and MCO's
    /// waiting players counted among its players, so the list closes when the game is full.
    /// </summary>
    void FillRoom(MCServiceRoom& room);

    /// <summary>One of MCO's players waiting in the room.</summary>
    struct MCWaiting
    {
        /// <summary>Its MCO name.</summary>
        std::string Name;
        bool Ready = false;
        bool operator==(const MCWaiting&) const = default;
    };

    /// <summary>
    /// MCO's players waiting in the internet game's room, the room's WAITING cards: a host's from its room's poll, a
    /// guest's as its host said. None once LET IN has let them in, until the match ends.
    /// </summary>
    std::vector<MCWaiting> Waiting();

    /// <summary>A guest: the host <paramref name="fromId"/> says who waits in its MCO room.</summary>
    void HostSaidWaiting(uint32_t fromId, std::span<const uint8_t> bytes);

    /// <summary>
    /// SPEC-mco-hosting.md's "The chat": <paramref name="text"/> (UTF-8), a line to all in the internet game's room,
    /// said in its MCO room too from this game's seat there (MCMcoRooms::SayBoth); nothing in a battle, or without a
    /// seat.
    /// </summary>
    void Said(std::string_view text);
    /// <summary>A guest: the host <paramref name="fromId"/> passes on a line of its MCO room (McoChat), put in the chat.</summary>
    void HostSaidChat(uint32_t fromId, std::span<const uint8_t> bytes);

    /// <summary>A guest: the host <paramref name="fromId"/> says its game is an MCO room too: the guest sits in it.</summary>
    void HostSaidRoom(uint32_t fromId, std::span<const uint8_t> bytes);
    /// <summary>The host: guest <paramref name="fromId"/>'s seat in the MCO room, as MCO answered it.</summary>
    void GuestSaidSeat(uint32_t fromId, std::span<const uint8_t> bytes);

    /// <summary>The game's internet game ends: a host's MCO room left, a guest's seat let go.</summary>
    void Ended();

    /// <summary>
    /// SPEC-mco-hosting.md's "When the host leaves": the host's LEAVE hands its session on to the machine of player
    /// <paramref name="successor"/> (MCDirectPlay::SuccessorPlayer), and its MCO room goes to that player's seat
    /// (MCMcoRooms::HandOnBoth). A successor with no seat there (an earlier build's, or one MCO refused): the room is
    /// left as the game ends (<see cref="Ended"/>), and the new host hosts its own.
    /// </summary>
    void HandOn(uint32_t successor);

    /// <summary>A player removed from the game (network/MCRemove.h), as MCO's room knows it, and when.</summary>
    struct MCKeptOut
    {
        /// <summary>Its MCO id, when its seat was known; empty otherwise.</summary>
        std::string ClientId;
        /// <summary>Its name: MCO's launcher names its game's player as its account.</summary>
        std::string Name;
        std::chrono::steady_clock::time_point At;
    };

    /// <summary>
    /// The host (SPEC-mco-hosting.md's "REMOVE"): a removed player out of the MCO room for MCRemove::KeptOutFor, its
    /// seat kicked (kick_peer) now and whenever it comes back, and never waiting: guest <paramref name="playerId"/>'s
    /// seat as it said, or the seat named <paramref name="name"/> (MCO's waiting players, and those let in).
    /// </summary>
    /// <returns>Whether the room had its seat.</returns>
    bool KeepOut(std::optional<uint32_t> playerId, std::string_view name);

    /// <summary>The host's LET IN: MCO's waiting players come into the game (MCO's START).</summary>
    void LetIn();
    /// <summary>
    /// Whether the host's last LET IN failed (MCO refused its START, or its relay couldn't be reached:
    /// MCMcoRooms::LetInFailedText), until LET IN is pressed again.
    /// </summary>
    bool LetInFailed();
    /// <summary>
    /// The host (SPEC-mco-hosting.md's "LET IN and the match"; an MCDirectPlay::JoinName): the session's name the
    /// joiner from <paramref name="ip"/> is told. A game MCO's relay carries (MCIceTransport::GuestAt's empty name) is
    /// told MCO's match's ("MCO " and 12 hex digits), as MCO's relay helper wants before its launcher's game counts as
    /// in; none for any other joiner, or while no match is on MCO.
    /// </summary>
    std::optional<std::string> JoinName(uint32_t ip);
    /// <summary>
    /// The match starts (MCRoomModel::PublishMatchState): a host's room starts on MCO, if LET IN hasn't, so MCO shows it
    /// in game; where MCO won't, the room is off MCO for the battle and hosted again after it.
    /// </summary>
    void MatchStarts();
    /// <summary>The match's scenario has ended: a host's MCO session closed, its room waiting again.</summary>
    void MatchOver();

    /// <summary>How often a host whose MCO room is gone (off for a battle, refused, MCO not answering) asks for one again.</summary>
    inline constexpr std::chrono::seconds RehostEvery{30};

    /// <summary>
    /// The switch's <c>hosts</c> part (SPEC-mco-hosting.md, "The switch"), as INTERNET's list last said: off until it
    /// has. A game already hosting keeps its MCO room.
    /// </summary>
    void SetHostsOn(bool on);
    bool HostsOn();

    // The parts the tests check.

    /// <summary>What a host's McoRoom says.</summary>
    struct MCRoomSaid
    {
        std::string RoomId;
        std::string Name;
        std::string Password;
        int32_t MaxPlayers = 0;
        bool operator==(const MCRoomSaid&) const = default;
    };

    /// <summary>The McoRoom for <paramref name="said"/> (texts cut to their fields).</summary>
    MCMPMcoRoomMessage RoomMessage(const MCRoomSaid& said);
    /// <summary>What <paramref name="bytes"/> say; none when they aren't an McoRoom of an MCO room.</summary>
    std::optional<MCRoomSaid> RoomSaidIn(std::span<const uint8_t> bytes);
    /// <summary>A guest's seat in its host's MCO room, as it says (McoSeat's value).</summary>
    enum class MCSeat : uint8_t
    {
        /// <summary>No seat: MCO refused one, or took it away, or MCO isn't in the game.</summary>
        None = 0,
        /// <summary>Asked for, MCO not yet answered.</summary>
        Asked = 1,
        Taken = 2,
    };

    /// <summary>What a guest's McoSeat says.</summary>
    struct MCSeatSaid
    {
        MCSeat Seat = MCSeat::None;
        /// <summary>Its MCO id, a seat taken; empty otherwise.</summary>
        std::string ClientId;
        bool operator==(const MCSeatSaid&) const = default;
    };

    /// <summary>The McoSeat for <paramref name="said"/>.</summary>
    MCMPMcoSeatMessage SeatMessage(const MCSeatSaid& said);
    /// <summary>What <paramref name="bytes"/> say; none when they aren't an McoSeat.</summary>
    std::optional<MCSeatSaid> SeatSaidIn(std::span<const uint8_t> bytes);

    /// <summary>How long a guest told the room has to say it asked for a seat before it's taken as having none.</summary>
    inline constexpr std::chrono::milliseconds AnswerWait{10000};

    /// <summary>A guest as its host knows it, for whether it has a seat in the MCO room.</summary>
    struct MCGuestSeat
    {
        /// <summary>The rules its build plays by, once heard (MCMatchRule).</summary>
        std::optional<uint32_t> Rules;
        /// <summary>When it was told the room.</summary>
        std::optional<std::chrono::steady_clock::time_point> Told;
        /// <summary>What it last said of its seat.</summary>
        std::optional<MCSeat> Said;
    };

    /// <summary>
    /// Whether <paramref name="guest"/> has no seat in the MCO room at <paramref name="now"/>: not Redux HD's build
    /// (no MCMatchRule::Rematch), silent past <see cref="AnswerWait"/> (an earlier build's), or MCO gave it none. A guest
    /// not yet heard from, or waiting on MCO, keeps its place.
    /// </summary>
    bool HasNoSeat(const MCGuestSeat& guest, std::chrono::steady_clock::time_point now);

    /// <summary>
    /// The most players the MCO room takes: the game's <paramref name="gameMost"/> less its players with no seat there,
    /// never below the seats MCO's room has taken, so MCO never takes more players than the game has room for.
    /// </summary>
    int32_t RoomMost(int32_t gameMost, size_t unseated, size_t seatsTaken);

    /// <summary>
    /// MCO's waiting players: the room's seats (<paramref name="seats"/>, the host's first) that aren't the host's, a
    /// guest's in the session (<paramref name="seated"/>, their MCO ids) or a player's already let in
    /// (<paramref name="names"/>, the session's players' names: MCO's launcher names its game's player as its account).
    /// </summary>
    std::vector<MCWaiting> WaitingIn(const std::vector<MCMcoApi::MCRoomPlayer>& seats,
                                     const std::set<std::string>& seated, const std::set<std::string>& names);

    /// <summary>
    /// The players our service's list counts in a host's room: the session's <paramref name="players"/>, then MCO's
    /// <paramref name="waiting"/> players while the room has room (<paramref name="most"/>).
    /// </summary>
    std::vector<std::string> RoomPlayers(std::vector<std::string> players, const std::vector<MCWaiting>& waiting,
                                         int32_t most);

    /// <summary>
    /// The MCO ids of the room's seats (<paramref name="seats"/>) that are a removed player's (<paramref name="kept"/>,
    /// by id, or by name for one whose seat wasn't known), never the host's.
    /// </summary>
    std::vector<std::string> KeptOutSeats(const std::vector<MCMcoApi::MCRoomPlayer>& seats,
                                          const std::vector<MCKeptOut>& kept);

    /// <summary>
    /// The session's lines for MCO's room lines (<paramref name="chat"/>) after the id <paramref name="after"/>: a waiting
    /// player's line (<paramref name="waitingIds"/>, their MCO ids) as "Clanner: text"; MCO's own of one joining that
    /// waits (<paramref name="waitingNames"/>) as "Clanner joined.", and of one gone (left, removed, its connection lost)
    /// that waited and isn't in the session (<paramref name="waited"/>) as "Clanner left."; the rest (the host's, a seated
    /// guest's, a player's let in, MCO's other sentences) left out.
    /// </summary>
    std::vector<std::string> SessionLinesOf(std::span<const MCMcoChatLine> chat, int64_t after,
                                            const std::set<std::string>& waitingIds,
                                            const std::set<std::string>& waitingNames,
                                            const std::set<std::string>& waited);

    /// <summary>The McoChat for <paramref name="line"/> (cut to its field), and what one says (none: it isn't one).</summary>
    MCMPMcoChatMessage ChatMessage(std::string_view line);
    std::optional<std::string> ChatSaidIn(std::span<const uint8_t> bytes);

    /// <summary>The McoWaiting for <paramref name="waiting"/> (its first six), and what one says (none: it isn't one).</summary>
    MCMPMcoWaitingMessage WaitingMessage(const std::vector<MCWaiting>& waiting);
    std::optional<std::vector<MCWaiting>> WaitingSaidIn(std::span<const uint8_t> bytes);

    // The tests' own (the room screen's tests, which have no MCO).

    /// <summary>
    /// The tests': while <paramref name="waiting"/> is set, <see cref="Waiting"/> gives it whatever the game, and
    /// <see cref="LetIn"/> only counts its presses, failing or not as <see cref="FailLetInForTests"/> last said
    /// (<see cref="LetInFailed"/>); none: MCO's own again, the presses and the failure forgotten.
    /// </summary>
    void SetWaitingForTests(std::optional<std::vector<MCWaiting>> waiting);
    /// <summary>The tests': whether the tests' LET IN fails, from its next press on.</summary>
    void FailLetInForTests(bool fail);
    /// <summary>The tests': LET IN's presses since the tests' waiting players were first set.</summary>
    int32_t LetInsForTests();
}
