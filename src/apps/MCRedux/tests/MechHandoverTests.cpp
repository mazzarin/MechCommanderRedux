#include "stdafx.h"
#include "MCTest.h"
#include "MCTestGuest.h"
#include "TestGame.h"
#include "ai/MCTacticalOrder.h"
#include "iface/MCCommandParser.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "main/MCGameContext.h"
#include "mission/MCScenario.h"
#include "network/MCMechHandover.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "network/MCMultiPlayerMessages.h"
#include "object/MCBattleMech.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"

// Port-only (Redux HD): a dropped player's units handed to a teammate still playing (network/MCMechHandover.h).

using MCMechHandover::MCHanded;
using MCMechHandover::MCTaker;

namespace
{
    /// <summary>A roster's slots from <paramref name="first"/> to its last.</summary>
    std::vector<int32_t> SlotsFrom(int32_t first)
    {
        std::vector<int32_t> slots;

        for (int32_t slot = first; slot < MCMultiPlayer::MaxLocalMovers; slot++)
        {
            slots.push_back(slot);
        }

        return slots;
    }

    /// <summary>A handover message as the network carries it: player <paramref name="leaver"/>'s <paramref name="units"/>.</summary>
    std::vector<uint8_t> HandoverBytes(int32_t leaver, std::initializer_list<MCHanded> units)
    {
        MCMPHandoverMessage message;
        message.Header = GuaranteedHeader(MCMPMessageType::Handover);
        message.Leaver = static_cast<int8_t>(leaver);

        for (const MCHanded& unit : units)
        {
            message.Units[message.Count++] = {.Part = static_cast<uint16_t>(unit.Part),
                                              .Player = static_cast<int8_t>(unit.Player),
                                              .Slot = static_cast<int8_t>(unit.Slot)};
        }

        std::vector<uint8_t> bytes(sizeof(message));
        std::memcpy(bytes.data(), &message, sizeof(message));
        return bytes;
    }

    /// <summary>The units a handover message (<paramref name="bytes"/>) hands on.</summary>
    std::vector<MCHanded> HandedIn(std::span<const uint8_t> bytes)
    {
        const auto message = ReadLinkupMessage<MCMPHandoverMessage>(bytes);
        std::vector<MCHanded> units;

        for (const MCMPHandoverMessage::Unit& unit :
             std::span(message.Units).first(std::min<size_t>(message.Count, message.Units.size())))
        {
            units.push_back({.Part = unit.Part, .Player = unit.Player, .Slot = unit.Slot});
        }

        return units;
    }

    /// <summary>Hands <paramref name="bytes"/> to the game as a message from player <paramref name="fromID"/>.</summary>
    void Deliver(uint32_t fromID, std::span<const uint8_t> bytes)
    {
        MCFidpMessage msg(0, 0x200);
        msg.FromID = fromID;
        msg.SetMessageBuffer(bytes);
        MultiPlayerApplicationCallback(msg);
    }

    /// <summary>A mech with part id <paramref name="part"/>, its commander <paramref name="commander"/>.</summary>
    std::shared_ptr<MCBattleMech> Mech(int32_t part, int32_t commander)
    {
        auto mech = std::make_shared<MCBattleMech>();
        mech->PartId = part;
        mech->SetCommanderId(commander);
        return mech;
    }
}

/// <summary>
/// The server hands a dropped player's units to the teammate with the fewest units still in the fight, ties to the
/// lowest player number, into its free slots first to last; those that don't fit go to the next by the same rule, and
/// any left after that stay. With no teammate able, nothing is handed.
/// </summary>
TEST_CASE("mech handover: the teammate with the fewest units takes them, ties to the lowest number, the rest the next")
{
    const std::array<int32_t, 5> parts = {600, 601, 602, 603, 604};
    // ANN (1) commands three units, EVE (4) and DAN (3) two each; DAN has two free slots, EVE ten.
    const std::vector<MCTaker> takers = {
        MCTaker{.Player = 1, .Units = 3, .FreeSlots = SlotsFrom(3)},
        MCTaker{.Player = 4, .Units = 2, .FreeSlots = SlotsFrom(2)},
        MCTaker{.Player = 3, .Units = 2, .FreeSlots = {4, 9}},
    };

    CHECK((MCMechHandover::Plan(parts, takers) ==
           std::vector<MCHanded>{{600, 3, 4}, {601, 3, 9}, {602, 4, 2}, {603, 4, 3}, {604, 4, 4}}));

    // One free slot among the teammates: one goes, the rest stay.
    const std::vector<MCTaker> full = {MCTaker{.Player = 2, .Units = 11, .FreeSlots = {11}}};
    CHECK((MCMechHandover::Plan(parts, full) == std::vector<MCHanded>{{600, 2, 11}}));
    CHECK(MCMechHandover::Plan(parts, {}).empty());
    CHECK(MCMechHandover::Plan({}, takers).empty());
}

/// <summary>
/// Every game takes the server's handover (only the server's) and moves the units into their new slots, the dropped
/// player's left empty; the receiver's game makes them its own, by those slots, as its orders name them, and the
/// server finds them there; any other game leaves them its player's as they were. The units keep their commander. A
/// unit the dropped player's roster doesn't hold, or a slot already taken, is skipped; the server takes no handover.
/// </summary>
TEST_CASE("mech handover: every game moves the units the server names into their new slots, the receiver's its own")
{
    constexpr uint32_t ServerId = 7;
    constexpr uint32_t OtherId = 9;
    constexpr int32_t Ann = 1;
    constexpr int32_t Bob = 2;

    // ANN's game (the receiver), then SALLY's (player 0, another's).
    for (const int32_t me : {Ann, 0})
    {
        MCTestContextScope scope;
        scope.Context().SetScenario(std::make_unique<MCScenario>());
        scope.Context().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        MCMultiPlayer& game = *scope.Context().MultiPlayer();
        NumLanPlayers = 3;
        game.InMission = true;
        game.ServerID = ServerId;
        game.CheckInId = me;

        const std::shared_ptr<MCBattleMech> annMech = Mech(MCMover::FirstPartId, Ann);
        const std::shared_ptr<MCBattleMech> bobFirst = Mech(MCMover::FirstPartId + 1, Bob);
        const std::shared_ptr<MCBattleMech> bobSecond = Mech(MCMover::FirstPartId + 2, Bob);
        const std::shared_ptr<MCBattleMech> bobWreck = Mech(MCMover::FirstPartId + 3, Bob);
        bobWreck->Status = 2;
        game.AddToPlayerMoverRoster(Ann, annMech);
        game.AddToPlayerMoverRoster(Bob, bobFirst);
        game.AddToPlayerMoverRoster(Bob, bobSecond);
        game.AddToPlayerMoverRoster(Bob, bobWreck);

        if (me == Ann)
        {
            game.AddToLocalMovers(annMech);
        }

        // What BOB's leave hands on: those still in the fight.
        CHECK((MCMechHandover::UnitsToHand(game, Bob) == std::vector<int32_t>{bobFirst->PartId, bobSecond->PartId}));

        const std::vector<uint8_t> handover =
            HandoverBytes(Bob, {{bobFirst->PartId, Ann, 1}, {bobSecond->PartId, Ann, 2}});
        Deliver(OtherId, handover);
        CHECK(LockObject(game.PlayerMoverRoster[Bob][0]) == bobFirst);

        Deliver(ServerId, handover);
        CHECK(LockObject(game.PlayerMoverRoster[Bob][0]) == nullptr);
        CHECK(LockObject(game.PlayerMoverRoster[Bob][1]) == nullptr);
        CHECK(LockObject(game.PlayerMoverRoster[Bob][2]) == bobWreck);
        CHECK(LockObject(game.PlayerMoverRoster[Ann][0]) == annMech);
        CHECK(LockObject(game.PlayerMoverRoster[Ann][1]) == bobFirst);
        CHECK(LockObject(game.PlayerMoverRoster[Ann][2]) == bobSecond);
        CHECK(MCMechHandover::Commands(game, Ann, *bobFirst));
        CHECK(!MCMechHandover::Commands(game, Bob, *bobFirst));
        CHECK_EQ(bobFirst->GetCommanderId(), Bob);

        if (me == Ann)
        {
            CHECK_EQ(bobFirst->NetPlayerId, 1);
            CHECK_EQ(bobSecond->NetPlayerId, 2);
            CHECK(LockObject(game.LocalMovers[2]) == bobSecond);
            CHECK_EQ(game.NumLocalMovers, 3);

            // Her order names them as she has them; the server's roster finds them so.
            MCTacticalOrder order;
            order.Reset();
            order.SetGroupFlag(bobFirst->NetPlayerId, true);
            order.SetGroupFlag(bobSecond->NetPlayerId, true);
            std::array<std::shared_ptr<MCMover>, MCMultiPlayer::MaxLocalMovers> ordered{};
            std::shared_ptr<MCMover> point;
            CHECK_EQ(order.GetGroup(Ann, ordered, point), 2);
            CHECK(ordered[0] == bobFirst);
            CHECK(ordered[1] == bobSecond);
        }
        else
        {
            CHECK_EQ(bobFirst->NetPlayerId, -1);
            CHECK_EQ(game.NumLocalMovers, 0);
        }

        // The wreck, to a slot taken (the rosters disagree), and a unit BOB no longer has: both skipped.
        Deliver(ServerId, HandoverBytes(Bob, {{bobWreck->PartId, Ann, 1}, {bobFirst->PartId, Ann, 3}}));
        CHECK(LockObject(game.PlayerMoverRoster[Bob][2]) == bobWreck);
        CHECK(LockObject(game.PlayerMoverRoster[Ann][3]) == nullptr);

        // The server moves them as it sends the handover: one from anyone else is dropped.
        game.IsServer = true;
        Deliver(ServerId, HandoverBytes(Bob, {{bobWreck->PartId, Ann, 4}}));
        CHECK(LockObject(game.PlayerMoverRoster[Bob][2]) == bobWreck);
        NumLanPlayers = 1;
    }
}

namespace
{
    /// <summary>The session the tests' matches play in.</summary>
    constexpr std::string_view MatchSession = "HANDOVER";
    /// <summary>The players a match's session holds.</summary>
    constexpr int32_t MatchPlaces = 6;

    /// <summary>A player of a test match: its number, its team, and the units of mission 1 it commands.</summary>
    struct MCForce
    {
        int32_t Player = 0;
        std::shared_ptr<MCTeam> Team;
        std::vector<int32_t> Parts;
    };

    /// <summary>
    /// Makes the booted mission a match on the loopback, hosted at <paramref name="port"/> by this game, player 0
    /// named <paramref name="name"/>: its game, the server. The match waits until <see cref="MakeMatch"/>.
    /// </summary>
    std::shared_ptr<MCMultiPlayer> HostMatch(std::string_view port, std::string_view name)
    {
        auto game = std::make_shared<MCMultiPlayer>();
        MCGameContext::Current().SetMultiPlayer(game);
        game->Start();
        game->SessionManager->ConnectTcp(std::format(":{}", port));
        REQUIRE_EQ(game->CreateSession(MatchSession, name, MatchPlaces), 0);
        game->CheckInId = 0;
        return game;
    }

    /// <summary>Has <paramref name="guest"/> join the match at <paramref name="port"/>; its player number (-1: none).</summary>
    int32_t JoinMatch(MCTestGuest::Guest& guest, std::string_view port)
    {
        MCSessionManager& session = *MultiPlayer()->SessionManager;

        if (!guest.Join(*session.DirectPlay, std::format("127.0.0.1:{}", port)))
        {
            return -1;
        }

        std::shared_ptr<MCFidpPlayer> player;
        MCTestGuest::SettleUntil(
            [&]
            {
                player = session.GetPlayer(guest.Id());
                return player != nullptr && player->HasPlayerNumber;
            });
        return player != nullptr && player->HasPlayerNumber ? player->PlayerNumber : -1;
    }

    /// <summary>
    /// Plays the match with <paramref name="forces"/>, as its load makes them: each player's commander (made, on its
    /// team, when the mission has none) leads its units in lance 0; every one is synchronised and in its player's
    /// roster, this game's player's its own; the mech bar shows this game's, the others' mechs kept aside for the
    /// results.
    /// </summary>
    void MakeMatch(MCMultiPlayer& game, std::span<const MCForce> forces)
    {
        for (uint32_t i = 1; i <= Scenario()->NumParts(); i++)
        {
            if (const auto mover = std::dynamic_pointer_cast<MCMover>(LockObject(Scenario()->Parts[i].Object)))
            {
                mover->NetPlayerId = -1;
            }
        }

        for (const MCForce& force : forces)
        {
            if (CommanderById(force.Player) == nullptr)
            {
                REQUIRE_EQ(NumCommanders(), force.Player);
                const auto commander = std::make_shared<MCCommander>(force.Player);
                commander->SetTeam(force.Team);
                Forces().Commanders.push_back(commander);
            }

            std::vector<std::shared_ptr<MCMover>> units;

            for (const int32_t part : force.Parts)
            {
                const std::shared_ptr<MCMover> unit = GetMoverFromPartId(part);
                REQUIRE(unit != nullptr);
                unit->SetCommanderId(force.Player);
                units.push_back(unit);
                game.AddToMoverRoster(unit);
                game.AddToPlayerMoverRoster(force.Player, unit);

                if (force.Player == game.CheckInId)
                {
                    game.AddToLocalMovers(unit);
                }
            }

            CommanderById(force.Player)->SetGroup(0, units, 0);
        }

        // The others' mechs kept aside (mission 1's clan vehicles have no icons: a match's players field none).
        const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
        iface->EndScenario();
        HomeCommander()->AddToGui(1);

        for (const MCForce& force : forces)
        {
            for (const int32_t part : force.Parts)
            {
                if (const std::shared_ptr<MCMover> unit = GetMoverFromPartId(part);
                    force.Player != game.CheckInId && unit->ObjectClass == MCObjectClass::BattleMech)
                {
                    iface->AddMech(part, 0, unit->GetAwake() != 0, false);
                }
            }
        }

        iface->StartScenario();
        game.InMission = true;
    }

    /// <summary>The units on the mech bar, left to right.</summary>
    std::vector<int32_t> BarParts()
    {
        std::vector<std::shared_ptr<MCFriendlyMechIcon>> buttons = TacticalInterface()->MechBar->Buttons();
        std::ranges::sort(buttons, {}, [](const std::shared_ptr<MCFriendlyMechIcon>& button) { return button->X(); });
        std::vector<int32_t> parts;

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : buttons)
        {
            parts.push_back(button->PartId);
        }

        return parts;
    }

    /// <summary>The units in player <paramref name="player"/>'s roster, by slot (0: none).</summary>
    std::array<int32_t, MCMultiPlayer::MaxLocalMovers> RosterParts(const MCMultiPlayer& game, int32_t player)
    {
        std::array<int32_t, MCMultiPlayer::MaxLocalMovers> parts{};

        for (size_t slot = 0; slot < parts.size(); slot++)
        {
            const std::shared_ptr<MCMover> unit = LockObject(game.PlayerMoverRoster[static_cast<size_t>(player)][slot]);
            parts[slot] = unit != nullptr ? unit->PartId : 0;
        }

        return parts;
    }

    /// <summary>How far apart <paramref name="a"/> and <paramref name="b"/> are on the ground.</summary>
    float GroundDistance(const MCVector3D& a, const MCVector3D& b)
    {
        return std::hypot(a.X - b.X, a.Y - b.Y);
    }

    /// <summary>A move order to <paramref name="goal"/>, as a click on the ground gives it.</summary>
    MCTacticalOrder MoveOrder(const MCVector3D& goal)
    {
        const MCWayPathPoint point{goal, false};
        MCTacticalOrder order;
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToPoint, 0);
        order.InitWayPath(std::span(&point, 1));
        order.MoveParams.Wait = 0;
        return order;
    }

    /// <summary>Whether <paramref name="unit"/>'s pilot runs a player's move order.</summary>
    bool MovingOnOrders(MCMover& unit)
    {
        const std::shared_ptr<MCMechWarrior> pilot = unit.GetPilot();
        return pilot != nullptr && pilot->CurTacOrder.Code == MCTacticalOrderCode::MoveToPoint &&
               pilot->CurTacOrder.Origin == MCOrderOrigin::Player;
    }

    /// <summary>The ground a unit covers on its way, at the least, for the tests to call it moved.</summary>
    constexpr float MovedAtLeast = 20.0f;
}

/// <summary>
/// Mission 1 as a match of three on the loopback, this game its host and server: ANN (this game's player) and BOB on the
/// Inner Sphere's side, SALLY on the clan's. BOB's game drops mid-battle: the server hands his mechs to ANN, his only
/// teammate, into her free slots, and tells every game (SALLY's sees the message); its rosters move them, BOB's left
/// empty; they become ANN's own, joining her mech bar after her own mech; they keep BOB as their commander. Her move
/// order, given through the interface, moves them.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a dropped player's mechs go to his teammate's mech bar after her own, and her orders move them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    constexpr std::string_view port = "28941";
    const std::shared_ptr<MCMultiPlayer> game = HostMatch(port, "ANN");
    MCTestGuest::Guest sally("SALLY", MCMatchRule::Supported);
    MCTestGuest::Guest bob("BOB", MCMatchRule::Supported);
    REQUIRE_EQ(JoinMatch(sally, port), 1);
    REQUIRE_EQ(JoinMatch(bob, port), 2);

    const std::array forces = {MCForce{.Player = 0, .Team = InnerSphereTeam(), .Parts = {512}},
                               MCForce{.Player = 1, .Team = ClanTeam(), .Parts = {896, 908, 909}},
                               MCForce{.Player = 2, .Team = InnerSphereTeam(), .Parts = {513, 514}}};
    MakeMatch(*game, forces);
    sally.Watch(MCMPMessageType::Handover);
    const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    CHECK((BarParts() == std::vector<int32_t>{512}));
    MCTestGuest::Settle();

    bob.Leave();
    REQUIRE(MCTestGuest::SettleUntil([&] { return !sally.Watched(MCMPMessageType::Handover).empty(); }));
    const std::vector<std::vector<uint8_t>> told = sally.Watched(MCMPMessageType::Handover);
    REQUIRE_EQ(told.size(), size_t{1});
    CHECK_EQ(int{ReadLinkupMessage<MCMPHandoverMessage>(told[0]).Leaver}, 2);
    const std::vector<MCHanded> handed = {{513, 0, 1}, {514, 0, 2}};
    CHECK((HandedIn(told[0]) == handed));

    CHECK((RosterParts(*game, 0) == std::array<int32_t, MCMultiPlayer::MaxLocalMovers>{512, 513, 514}));
    CHECK((RosterParts(*game, 2) == std::array<int32_t, MCMultiPlayer::MaxLocalMovers>{}));
    std::vector<std::shared_ptr<MCMover>> units;

    for (const MCHanded& unit : handed)
    {
        const std::shared_ptr<MCMover> mover = GetMoverFromPartId(unit.Part);
        REQUIRE(mover != nullptr);
        CHECK_EQ(mover->NetPlayerId, unit.Slot);
        CHECK_EQ(mover->GetCommanderId(), 2);
        units.push_back(mover);
    }

    REQUIRE(MCTestGuest::SettleUntil([&] { return !iface->MechBar->Dancing; }));
    CHECK((BarParts() == std::vector<int32_t>{512, 513, 514}));

    // ANN selects them and orders them toward the clan's Uller.
    const std::shared_ptr<MCMover> uller = GetMoverFromPartId(896);
    REQUIRE(uller != nullptr);
    const MCVector3D goal = uller->GetPosition();
    iface->ClearMechSelection();

    for (const std::shared_ptr<MCMover>& unit : units)
    {
        iface->SelectMech(unit->PartId);
        iface->CommandParser->AddSubject(unit->PartId);
    }

    std::vector<float> before;

    for (const std::shared_ptr<MCMover>& unit : units)
    {
        before.push_back(GroundDistance(unit->GetPosition(), goal));
    }

    REQUIRE(iface->CommandParser->SendTacOrder(MoveOrder(goal), true));
    MCTestGuest::Settle();

    for (size_t i = 0; i < units.size(); i++)
    {
        CHECK(MovingOnOrders(*units[i]));
        CHECK(GroundDistance(units[i]->GetPosition(), goal) < before[i] - MovedAtLeast);
    }

    sally.Leave();
}

/// <summary>
/// Mission 1 as a match of five on the loopback, this game (SALLY, the Inner Sphere's) its host and server; BOB, ANN,
/// DAN and EVE on the clan's side, ANN's game without the rule. BOB drops: ANN has the fewest units, but is passed over;
/// DAN, with fewer than EVE, takes BOB's into his free slots, and DAN's move order for those slots, over the network,
/// moves them (and not his own); his LIGHTS turn them, the server telling every game as BOB's. DAN drops in turn: all he
/// commands, his own and BOB's, go to EVE. EVE drops with only ANN left on her side: nothing is handed on, and her
/// units stand in her roster as they were.
/// </summary>
TEST_CASE_ISOLATED("game: a dropped player's units pass over a teammate whose game can't take them, and go on as "
                   "their receiver drops")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    constexpr std::string_view port = "28943";
    const std::shared_ptr<MCMultiPlayer> game = HostMatch(port, "SALLY");
    MCTestGuest::Guest bob("BOB", MCMatchRule::Supported);
    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported & ~MCMatchRule::MechHandover);
    MCTestGuest::Guest dan("DAN", MCMatchRule::Supported);
    MCTestGuest::Guest eve("EVE", MCMatchRule::Supported);
    REQUIRE_EQ(JoinMatch(bob, port), 1);
    REQUIRE_EQ(JoinMatch(ann, port), 2);
    REQUIRE_EQ(JoinMatch(dan, port), 3);
    REQUIRE_EQ(JoinMatch(eve, port), 4);

    const std::array forces = {MCForce{.Player = 0, .Team = InnerSphereTeam(), .Parts = {512, 513, 514}},
                               MCForce{.Player = 1, .Team = ClanTeam(), .Parts = {920, 933, 968}},
                               MCForce{.Player = 2, .Team = ClanTeam(), .Parts = {908}},
                               MCForce{.Player = 3, .Team = ClanTeam(), .Parts = {921, 932}},
                               MCForce{.Player = 4, .Team = ClanTeam(), .Parts = {909, 934, 956, 980}}};
    MakeMatch(*game, forces);

    for (MCTestGuest::Guest& guest : {std::ref(ann), std::ref(dan), std::ref(eve)})
    {
        guest.Watch(MCMPMessageType::Handover);
        guest.Watch(MCMPMessageType::Lights);
    }

    MCTestGuest::Settle();
    using Roster = std::array<int32_t, MCMultiPlayer::MaxLocalMovers>;

    // BOB drops.
    bob.Leave();
    REQUIRE(MCTestGuest::SettleUntil(
        [&]
        {
            return !dan.Watched(MCMPMessageType::Handover).empty() && !eve.Watched(MCMPMessageType::Handover).empty();
        }));
    const std::vector<MCHanded> toDan = {{920, 3, 2}, {933, 3, 3}, {968, 3, 4}};
    CHECK_EQ(int{ReadLinkupMessage<MCMPHandoverMessage>(dan.Watched(MCMPMessageType::Handover)[0]).Leaver}, 1);
    CHECK((HandedIn(dan.Watched(MCMPMessageType::Handover)[0]) == toDan));
    CHECK((HandedIn(eve.Watched(MCMPMessageType::Handover)[0]) == toDan));
    CHECK((RosterParts(*game, 3) == Roster{921, 932, 920, 933, 968}));
    CHECK((RosterParts(*game, 1) == Roster{}));
    CHECK((RosterParts(*game, 2) == Roster{908}));
    CHECK_EQ(GetMoverFromPartId(920)->NetPlayerId, -1);

    // DAN's order for his new slots, as his game sends it: to the ground between two of them.
    const MCVector3D goal = (GetMoverFromPartId(920)->GetPosition() + GetMoverFromPartId(933)->GetPosition()) * 0.5f;
    MCTacticalOrder order = MoveOrder(goal);

    for (const MCHanded& unit : toDan)
    {
        order.SetGroupFlag(unit.Slot, true);
    }

    order.Pack();
    MCMPPlayerOrderMessage message{};
    message.Header = PlainHeader(MCMPMessageType::PlayerOrder);
    message.CheckInId = 3;
    message.OrderParam1 = std::bit_cast<uint32_t>(goal.X);
    message.OrderParam2 = std::bit_cast<uint32_t>(goal.Y);
    message.PackedOrder = {order.Data[0], order.Data[1]};
    std::vector<std::pair<std::shared_ptr<MCMover>, float>> ordered;

    for (const MCHanded& unit : toDan)
    {
        const std::shared_ptr<MCMover> mover = GetMoverFromPartId(unit.Part);
        ordered.emplace_back(mover, GroundDistance(mover->GetPosition(), goal));
    }

    dan.SendTo(game->SessionManager->MyPlayer()->Id, message);
    REQUIRE(MCTestGuest::SettleUntil(
        [&] { return std::ranges::all_of(ordered, [](const auto& unit) { return MovingOnOrders(*unit.first); }); }));
    MCTestGuest::Settle();

    for (const auto& [unit, distance] : ordered)
    {
        CHECK(GroundDistance(unit->GetPosition(), goal) < distance - MovedAtLeast);
    }

    CHECK(!MovingOnOrders(*GetMoverFromPartId(921)));
    CHECK(!MovingOnOrders(*GetMoverFromPartId(932)));

    // DAN's LIGHTS for them, in a match whose rules have them: the server turns them off, and tells every game as their
    // commander's (BOB's), as every game takes a player's lights for its own units.
    game->Played.Rules = MCMatchRule::NightLights;
    MCMPLightsMessage lights{};
    lights.Header = PlainHeader(MCMPMessageType::Lights);
    lights.CheckInId = 3;

    for (const MCHanded& unit : toDan)
    {
        lights.Parts[lights.Count++] = static_cast<uint16_t>(unit.Part);
    }

    REQUIRE(std::ranges::all_of(ordered, [](const auto& unit) { return unit.first->LightsOn; }));
    dan.SendTo(game->SessionManager->MyPlayer()->Id, lights);
    REQUIRE(MCTestGuest::SettleUntil([&] { return !eve.Watched(MCMPMessageType::Lights).empty(); }));
    CHECK(std::ranges::none_of(ordered, [](const auto& unit) { return unit.first->LightsOn; }));
    const auto relayed = ReadLinkupMessage<MCMPLightsMessage>(eve.Watched(MCMPMessageType::Lights)[0]);
    CHECK_EQ(int{relayed.CheckInId}, 1);
    CHECK_EQ(int{relayed.Count}, 3);
    CHECK_EQ(int{relayed.Parts[0]}, 920);
    game->Played.Rules = std::nullopt;

    // DAN drops in turn.
    dan.Leave();
    REQUIRE(MCTestGuest::SettleUntil([&] { return eve.Watched(MCMPMessageType::Handover).size() == 2; }));
    CHECK_EQ(int{ReadLinkupMessage<MCMPHandoverMessage>(eve.Watched(MCMPMessageType::Handover)[1]).Leaver}, 3);
    CHECK((HandedIn(eve.Watched(MCMPMessageType::Handover)[1]) ==
           std::vector<MCHanded>{{921, 4, 4}, {932, 4, 5}, {920, 4, 6}, {933, 4, 7}, {968, 4, 8}}));
    CHECK((RosterParts(*game, 4) == Roster{909, 934, 956, 980, 921, 932, 920, 933, 968}));
    CHECK((RosterParts(*game, 3) == Roster{}));

    // EVE drops with only ANN on her side.
    const uint32_t eveId = eve.Id();
    eve.Leave();
    REQUIRE(MCTestGuest::SettleUntil([&] { return game->SessionManager->GetPlayer(eveId) == nullptr; }));
    MCTestGuest::Settle();
    CHECK_EQ(ann.Watched(MCMPMessageType::Handover).size(), size_t{2});
    CHECK((RosterParts(*game, 4) == Roster{909, 934, 956, 980, 921, 932, 920, 933, 968}));
    ann.Leave();
}
