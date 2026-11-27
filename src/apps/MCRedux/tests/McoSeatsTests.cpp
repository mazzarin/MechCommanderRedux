#include "stdafx.h"
#include "MCTest.h"
#include "network/MCMcoSeats.h"
#include "network/MCMultiPlayer.h"

#include <chrono>

#include <string>
#include <vector>

// Port-only (Redux HD; SPEC-mco-hosting.md, "Seats"): what an internet game's host and guests say of its MCO room.

namespace
{
    const std::string RoomId = "0123456789abcdef0123456789abcdef";
    const std::string ClientId = "fedcba9876543210fedcba9876543210";
}

TEST_CASE("mco seats: the host's word of its MCO room reaches the guest whole, and nothing else is taken for one")
{
    const MCMcoSeats::MCRoomSaid said{.RoomId = RoomId, .Name = "Steel Rain", .Password = "K7QF", .MaxPlayers = 6};
    MCMPMcoRoomMessage message = MCMcoSeats::RoomMessage(said);
    CHECK(MCMcoSeats::RoomSaidIn(LinkupBytes(message)) == std::optional(said));

    // A long name is cut to the service's game names (24 characters); a public game has no password.
    MCMPMcoRoomMessage longer =
        MCMcoSeats::RoomMessage({.RoomId = RoomId, .Name = std::string(40, 'A'), .Password = {}, .MaxPlayers = 4});
    const std::optional<MCMcoSeats::MCRoomSaid> cut = MCMcoSeats::RoomSaidIn(LinkupBytes(longer));
    REQUIRE(cut.has_value());
    CHECK_EQ(cut->Name, std::string(24, 'A'));
    CHECK(cut->Password.empty());

    // Not an MCO room's id, no name, or a room's size MCO doesn't take; too short.
    MCMPMcoRoomMessage bad = message;
    bad.RoomId[3] = 'x';
    CHECK(!MCMcoSeats::RoomSaidIn(LinkupBytes(bad)).has_value());
    bad = message;
    bad.Name.fill('\0');
    CHECK(!MCMcoSeats::RoomSaidIn(LinkupBytes(bad)).has_value());
    bad = message;
    bad.Name.fill('A');
    CHECK(!MCMcoSeats::RoomSaidIn(LinkupBytes(bad)).has_value());
    bad = message;
    bad.MaxPlayers = 7;
    CHECK(!MCMcoSeats::RoomSaidIn(LinkupBytes(bad)).has_value());
    CHECK(!MCMcoSeats::RoomSaidIn(LinkupBytes(message, sizeof(message) - 1)).has_value());
}

TEST_CASE("mco seats: a guest's seat reaches the host as asked, taken with its MCO id, or none")
{
    using MCMcoSeats::MCSeat;
    using MCMcoSeats::MCSeatSaid;

    for (const MCSeatSaid& said : {MCSeatSaid{.Seat = MCSeat::Asked}, MCSeatSaid{.Seat = MCSeat::None},
                                   MCSeatSaid{.Seat = MCSeat::Taken, .ClientId = ClientId}})
    {
        MCMPMcoSeatMessage message = MCMcoSeats::SeatMessage(said);
        CHECK(MCMcoSeats::SeatSaidIn(LinkupBytes(message)) == std::optional(said));
    }

    // A seat taken without an MCO id, or with one that isn't; a seat MCO doesn't name; too short.
    MCMPMcoSeatMessage bad = MCMcoSeats::SeatMessage({.Seat = MCSeat::Taken, .ClientId = "not an id"});
    CHECK(!MCMcoSeats::SeatSaidIn(LinkupBytes(bad)).has_value());
    bad = MCMcoSeats::SeatMessage({.Seat = MCSeat::Taken});
    CHECK(!MCMcoSeats::SeatSaidIn(LinkupBytes(bad)).has_value());
    bad.Seat = 3;
    CHECK(!MCMcoSeats::SeatSaidIn(LinkupBytes(bad)).has_value());
    MCMPMcoSeatMessage asked = MCMcoSeats::SeatMessage({.Seat = MCSeat::Asked});
    CHECK(!MCMcoSeats::SeatSaidIn(LinkupBytes(asked, sizeof(asked) - 1)).has_value());
}

TEST_CASE("mco seats: a guest has no seat when its build isn't Redux HD's, it stays silent past the wait, or MCO gave "
          "it none")
{
    using MCMcoSeats::MCSeat;
    const auto told = std::chrono::steady_clock::now();
    const auto later = told + MCMcoSeats::AnswerWait;
    constexpr uint32_t reduxHd = MCMatchRule::Supported;
    constexpr uint32_t upstream = MCMatchRule::Supported & ~MCMatchRule::Rematch;

    // Not yet heard from: its place kept.
    CHECK(!MCMcoSeats::HasNoSeat({}, later));
    CHECK(MCMcoSeats::HasNoSeat({.Rules = upstream}, told));
    // Told the room: kept until the wait is up; an earlier Redux HD build never answers.
    CHECK(!MCMcoSeats::HasNoSeat({.Rules = reduxHd}, later));
    CHECK(!MCMcoSeats::HasNoSeat({.Rules = reduxHd, .Told = told}, told));
    CHECK(MCMcoSeats::HasNoSeat({.Rules = reduxHd, .Told = told}, later));
    // Asked and waiting on MCO, or seated: kept however long; given none: not.
    CHECK(!MCMcoSeats::HasNoSeat({.Rules = reduxHd, .Told = told, .Said = MCSeat::Asked}, later));
    CHECK(!MCMcoSeats::HasNoSeat({.Rules = reduxHd, .Told = told, .Said = MCSeat::Taken}, later));
    CHECK(MCMcoSeats::HasNoSeat({.Rules = reduxHd, .Told = told, .Said = MCSeat::None}, told));
}

TEST_CASE("mco seats: MCO's room takes no more players than the game has room for, and never fewer than it seats")
{
    // A 6-player game with every guest seated, then two without a seat.
    CHECK_EQ(MCMcoSeats::RoomMost(6, 0, 3), 6);
    CHECK_EQ(MCMcoSeats::RoomMost(6, 2, 3), 4);
    // Never below the seats taken, nor MCO's fewest.
    CHECK_EQ(MCMcoSeats::RoomMost(4, 3, 3), 3);
    CHECK_EQ(MCMcoSeats::RoomMost(2, 2, 1), 2);
}

TEST_CASE("mco seats: MCO's waiting players are the room's seats that aren't the host's or a guest's in the game")
{
    const std::vector<MCMcoApi::MCRoomPlayer> seats{
        {.ClientId = "a", .Username = "ANN", .Ready = true, .Host = true},
        {.ClientId = "b", .Username = "BOB", .Ready = true},
        {.ClientId = "c", .Username = "CLANNER", .Ready = false},
    };

    using MCMcoSeats::MCWaiting;
    const std::vector<MCWaiting> clanner{{.Name = "CLANNER", .Ready = false}};
    CHECK((MCMcoSeats::WaitingIn(seats, {"b"}, {"ANN", "BOB"}) == clanner));
    CHECK((MCMcoSeats::WaitingIn(seats, {"b", "c"}, {}).empty()));
    // Let in: its game's player is named as its MCO account.
    CHECK((MCMcoSeats::WaitingIn(seats, {"b"}, {"ANN", "BOB", "CLANNER"}).empty()));
    const std::vector<MCWaiting> both{{.Name = "BOB", .Ready = true}, {.Name = "CLANNER", .Ready = false}};
    CHECK((MCMcoSeats::WaitingIn(seats, {}, {}) == both));
}

TEST_CASE("mco seats: our list counts MCO's waiting players in the host's room, never past its most")
{
    using MCMcoSeats::MCWaiting;
    const std::vector<MCWaiting> waiting{{.Name = "CLANNER", .Ready = true}, {.Name = "KERENSKY"}};
    const std::vector<std::string> all{"ANN", "BOB", "CLANNER", "KERENSKY"};
    CHECK((MCMcoSeats::RoomPlayers({"ANN", "BOB"}, waiting, 6) == all));
    // A join meanwhile filled the places the door held: the room the service gets is never over its most.
    const std::vector<std::string> three{"ANN", "BOB", "CLANNER"};
    CHECK((MCMcoSeats::RoomPlayers({"ANN", "BOB"}, waiting, 3) == three));
    const std::vector<std::string> full{"ANN", "BOB"};
    CHECK((MCMcoSeats::RoomPlayers({"ANN", "BOB"}, waiting, 2) == full));
    CHECK((MCMcoSeats::RoomPlayers({"ANN", "BOB"}, {}, 6) == full));
}

TEST_CASE("mco seats: a removed player's seat is kicked by its MCO id, or by name when its seat wasn't known")
{
    const std::vector<MCMcoApi::MCRoomPlayer> seats{
        {.ClientId = "a", .Username = "ANN", .Ready = true, .Host = true},
        {.ClientId = "b", .Username = "BOB", .Ready = true},
        {.ClientId = "c", .Username = "CLANNER", .Ready = false},
    };

    using MCMcoSeats::MCKeptOut;
    const std::vector<std::string> bob{"b"};
    CHECK((MCMcoSeats::KeptOutSeats(seats, {MCKeptOut{.ClientId = "b", .Name = "ROBERT"}}) == bob));
    const std::vector<std::string> clanner{"c"};
    CHECK((MCMcoSeats::KeptOutSeats(seats, {MCKeptOut{.Name = "CLANNER"}}) == clanner));
    // The host is never taken out of its own room.
    CHECK(MCMcoSeats::KeptOutSeats(seats, {MCKeptOut{.ClientId = "a", .Name = "ANN"}}).empty());
    CHECK(MCMcoSeats::KeptOutSeats(seats, {}).empty());
}

TEST_CASE("mco seats: who waits in the host's MCO room reaches its guests whole, six at most")
{
    using MCMcoSeats::MCWaiting;
    const std::vector<MCWaiting> waiting{{.Name = "CLANNER", .Ready = true}, {.Name = "SHINOBI", .Ready = false}};
    MCMPMcoWaitingMessage message = MCMcoSeats::WaitingMessage(waiting);
    CHECK(MCMcoSeats::WaitingSaidIn(LinkupBytes(message)) == std::optional(waiting));
    MCMPMcoWaitingMessage none = MCMcoSeats::WaitingMessage({});
    CHECK(MCMcoSeats::WaitingSaidIn(LinkupBytes(none)) == std::optional(std::vector<MCWaiting>()));
    MCMPMcoWaitingMessage many = MCMcoSeats::WaitingMessage(std::vector<MCWaiting>(8, {.Name = "X"}));
    CHECK_EQ(many.Count, 6);

    MCMPMcoWaitingMessage bad = message;
    bad.Count = 7;
    CHECK(!MCMcoSeats::WaitingSaidIn(LinkupBytes(bad)).has_value());
    bad = message;
    bad.Players[1].Name.fill('\0');
    CHECK(!MCMcoSeats::WaitingSaidIn(LinkupBytes(bad)).has_value());
    bad = message;
    bad.Players[0].Ready = 2;
    CHECK(!MCMcoSeats::WaitingSaidIn(LinkupBytes(bad)).has_value());
}
