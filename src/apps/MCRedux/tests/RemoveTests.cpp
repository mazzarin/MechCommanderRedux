#include "stdafx.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "TestGame.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRemove.h"
#include "network/MCRoomEnd.h"
#include "platform/MCDirectPlay.h"

#include <chrono>
#include <format>
#include <string>
#include <utility>
#include <vector>

// Port-only (Redux HD; SPEC-mco-hosting.md's "REMOVE"): the host takes a player out of its room and keeps them out a
// while. The game's side is the test game; the others' are the tests' players (tests/MCTestGuest.h) on the loopback.

using namespace std::chrono_literals;
using MCTestGuest::DialogText;
using MCTestGuest::Guest;
using MCTestGuest::Host;
using MCTestGuest::InSession;
using MCTestGuest::JoinFromLan;
using MCTestGuest::OnLan;
using MCTestGuest::Settle;
using MCTestGuest::SettleUntil;

namespace
{
    /// <summary>The host's REMOVE's word, as the tests' players hear it.</summary>
    constexpr int32_t RemovedWord = std::to_underlying(MCRoomEnd::MCHow::Removed);
}

TEST_CASE("remove: a join kept out names the game and the whole minutes left, and the door's refusal reads back")
{
    CHECK_EQ(MCRemove::KeptOutLine("Steel Rain", 480s),
             std::string("You were removed from Steel Rain. Try again in 8 minutes."));
    CHECK_EQ(MCRemove::KeptOutLine("Steel Rain", 421s),
             std::string("You were removed from Steel Rain. Try again in 8 minutes."));
    CHECK_EQ(MCRemove::KeptOutLine("Steel Rain", 60s),
             std::string("You were removed from Steel Rain. Try again in 1 minute."));
    CHECK_EQ(MCRemove::KeptOutLine("Steel Rain", 0s),
             std::string("You were removed from Steel Rain. Try again in 1 minute."));
    CHECK_EQ(MCRemove::RemovedLine("Ann"), std::string("Ann removed you from the game."));

    CHECK(MCRemove::DoorLeft(MCRemove::DoorReason(480s)) == std::optional(480s));
    // A refusal for options the game lacks isn't the door's.
    CHECK(!MCRemove::DoorLeft("weather").has_value());
    CHECK(!MCRemove::DoorLeft("removed soon").has_value());
    CHECK(!MCRemove::DoorLeft("removed -5").has_value());
}

/// <summary>
/// The host's REMOVE on a LAN guest: its game is told, its machine dropped a moment later (this test's player doesn't
/// leave on the word, as an earlier build wouldn't), and its computer turned away at the door for 10 minutes, saying
/// how long is left; after them it joins again. The host never removes itself.
/// </summary>
TEST_CASE_ISOLATED("game: the host's REMOVE takes a LAN guest out and keeps its computer out for 10 minutes")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Address = "127.0.0.1:28980";
    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(Address);
    MCTestGuest::HostOnLan("Bob", "Steel Rain", 6);
    Settle();
    REQUIRE(manager.DirectPlay != nullptr);
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    MCRemove::SetClockForTests([&now] { return now; });

    Guest ann("Ann");
    REQUIRE(ann.Join(*manager.DirectPlay, Address));
    REQUIRE(SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()); }));
    CHECK(!MCRemove::Player(manager.MyPlayerID));

    REQUIRE(MCRemove::Player(ann.Id()));
    REQUIRE(SettleUntil([&] { return ann.Lost(); }));
    Settle();
    CHECK((ann.RoomEnds() == std::vector<int32_t>{RemovedWord}));
    CHECK(manager.GetPlayer(ann.Id()) == nullptr);
    CHECK(!MCTestGuest::RoomShows(ann.Id()));

    // Back from the same computer: turned away at the door, saying how long is left.
    Guest again("Ann");
    CHECK(!again.Join(*manager.DirectPlay, Address));
    CHECK(MCRemove::DoorLeft(again.Refusal()) == std::optional(std::chrono::seconds(MCRemove::KeptOutFor)));
    now += 7min;
    Guest later("Ann");
    CHECK(!later.Join(*manager.DirectPlay, Address));
    CHECK(MCRemove::DoorLeft(later.Refusal()) == std::optional(180s));

    // Its 10 minutes up, it comes in.
    now += 3min;
    Guest back("Ann");
    CHECK(back.Join(*manager.DirectPlay, Address));
    MCRemove::SetClockForTests({});
}

/// <summary>
/// A LAN guest whose host removes its player: back to the LAN games, saying who removed it; joining while kept out says
/// how long is left (the host's door's refusal, in the screens' words).
/// </summary>
TEST_CASE_ISOLATED("game: a LAN guest its host removes goes back to the LAN games, saying who, and is told when to try "
                   "again")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Port = "28982";
    REQUIRE(MCTestGame::StartLogistics());
    MCTestGuest::OpenLan();

    Host bob;
    REQUIRE(bob.Open(Port, "BOB", "BOB'S GAME"));
    JoinFromLan(Port, "ANN");
    bob.SendRoomEnds(MultiPlayer()->SessionManager->MyPlayerID, RemovedWord);
    REQUIRE(SettleUntil([] { return !InSession(); }));
    Settle();
    CHECK(OnLan());
    CHECK_EQ(DialogText(), std::string("BOB removed you from the game."));
    MCTestGuest::PressDialogOk();
    Settle();

    // Joining again while kept out: the door's refusal.
    bob.RefuseWith(MCRemove::DoorReason(421s));
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(std::format("127.0.0.1:{}", Port));
    MCGamesScreen& lan = *MCTestGuest::Games(MCGamesWhere::Lan);
    REQUIRE(SettleUntil([&] { return !lan.Seen().Rows.empty(); }));
    lan.PickRow(0);
    lan.PressJoin();
    REQUIRE(SettleUntil([] { return !DialogText().empty(); }));
    CHECK(!InSession());
    CHECK_EQ(DialogText(), std::string("You were removed from BOB'S GAME. Try again in 8 minutes."));
    bob.Close();
}
