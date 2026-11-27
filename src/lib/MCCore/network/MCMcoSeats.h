#pragma once

// Port-only (Redux HD; SPEC-mco-hosting.md): an internet game this game hosts from INTERNET is an MCO room too
// (network/MCMcoRooms' HostBoth), its MCO guests coming in over MCO's relay as links of the game's own. The host's room
// carries the mission as its description, and counts the players who have no seat in it: each Redux HD guest is told
// the room (MCMPMessageType::McoRoom), and one whose build has seats asks for one at once (McoSeat), sits in it quietly
// (Sit) and tells the host how MCO answered. A guest that never answers is an earlier build's, with no seat. Nothing
// of it shows on the screens: a room or a seat MCO refuses is only logged, and the game plays on our service alone.

#include "network/MCMcoApi.h"
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

    /// <summary>A guest: the host <paramref name="fromId"/> says its game is an MCO room too: the guest sits in it.</summary>
    void HostSaidRoom(uint32_t fromId, std::span<const uint8_t> bytes);
    /// <summary>The host: guest <paramref name="fromId"/>'s seat in the MCO room, as MCO answered it.</summary>
    void GuestSaidSeat(uint32_t fromId, std::span<const uint8_t> bytes);

    /// <summary>The game's internet game ends: a host's MCO room left, a guest's seat let go.</summary>
    void Ended();

    /// <summary>The host's LET IN: MCO's waiting players come into the game (MCO's START).</summary>
    void LetIn();
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
    /// MCO's waiting players: the room's seats (<paramref name="seats"/>, the host's first) that aren't the host's and
    /// aren't a guest's in the session (<paramref name="inSession"/>, their MCO ids); their names.
    /// </summary>
    std::vector<std::string> WaitingIn(const std::vector<MCMcoApi::MCRoomPlayer>& seats,
                                       const std::set<std::string>& inSession);
}
