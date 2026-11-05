#include "stdafx.h"
#include "MCTest.h"
#include "MCMock.h"
#include "linkup/MCFidpMessage.h"
#include "main/MCGameContext.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCBattleMech.h"
#include "object/MCEffectSystem.h"
#include "object/MCFire.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechWarrior.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMiscTerrainObjectType.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectTypeManager.h"
#include "object/MCTurret.h"
#include "object/MCTurretType.h"
#include "object/MCWeaponFireChunk.h"
#include "object/MCWeaponShotInfo.h"

// The game's side of multiplayer: the packed world-state and weapon-hit chunks the server sends, the queues they wait
// in, and the handlers that apply the server's updates on a client.

namespace
{
    /// <summary>Gives <paramref name="scope"/> a new multiplayer game (no session).</summary>
    MCMultiPlayer& InstallGame(MCTestContextScope& scope)
    {
        scope.Context().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        return *scope.Context().MultiPlayer();
    }

    /// <summary>A mover whose network updates the test can watch.</summary>
    class FakeMover : public MCBattleMech
    {
    public:
        int32_t HandleMoveChunk(uint32_t chunk) override
        {
            MoveChunks(chunk);
            return 0;
        }

        int32_t HandleStatusChunk(int32_t updateAge, uint32_t chunk) override
        {
            StatusChunks(updateAge, chunk);
            return 0;
        }

        std::shared_ptr<MCMechWarrior> GetPilot() override { return FakePilot; }

        MCMock::Calls<uint32_t> MoveChunks;
        MCMock::Calls<int32_t, uint32_t> StatusChunks;
        std::shared_ptr<MCMechWarrior> FakePilot = std::make_shared<MCMechWarrior>();
    };

    /// <summary>A client of a two-player LAN game in a running mission.</summary>
    struct ClientGame
    {
        ClientGame()
        {
            Scope.Context().SetScenario(std::make_unique<MCScenario>());
            InstallGame(Scope);
            Game = Scope.Context().MultiPlayer();
            NumLanPlayers = 2;
            Game->InMission = true;
        }

        ~ClientGame() { NumLanPlayers = 1; }

        MCTestContextScope Scope;
        /// <summary>The game, pinned until the scope ends.</summary>
        std::shared_ptr<MCMultiPlayer> Game;
    };

    /// <summary>A message of the game's: its header word, then <paramref name="body"/>.</summary>
    std::vector<uint8_t> Message(MCMPMessageType type, std::initializer_list<uint8_t> body)
    {
        std::vector<uint8_t> bytes{static_cast<uint8_t>(type), 0};
        bytes.insert(bytes.end(), body);
        return bytes;
    }

    /// <summary>Appends a little-endian word.</summary>
    void PutWord(std::vector<uint8_t>& bytes, uint32_t word)
    {
        for (int i = 0; i < 4; i++)
        {
            bytes.push_back(static_cast<uint8_t>(word >> (8 * i)));
        }
    }

    /// <summary>A chunk packed, then unpacked again from its word alone.</summary>
    MCWorldStateChunk RoundTrip(MCWorldStateChunk& chunk)
    {
        chunk.Pack();
        MCWorldStateChunk received;
        received.Data = chunk.Data;
        received.Unpack();
        return received;
    }
}

/// <summary>
/// Each world event packs into one word, the 4-bit type at the bottom: a mine (column 4-13, row 14-23, state 24-26,
/// team 27), a terrain fire (item 4-6, vertex 7-15, block 16-23, seconds 24-31), an artillery strike (column, row,
/// seconds + 1 at 24-28, strike type 29-31), a script message (message 4-11, value + 32000 at 12-27) and a pilot's kill
/// (kind 4-6, mover 7-11).
/// </summary>
TEST_CASE("network: each world-state chunk packs into its word and unpacks to the same event")
{
    MCWorldStateChunk mine;
    mine.BuildMine(5, 7, 1, 3, 2);
    MCWorldStateChunk received = RoundTrip(mine);
    CHECK_EQ(mine.Data, (7u << 4) | (5u << 14) | (5u << 24) | (1u << 27));
    CHECK(received.EqualTo(mine));
    CHECK_EQ(received.Param2, 5);

    // A fire on terrain object 0x1000 + block 3 * 0xc80 + vertex 17 * 8 + item 5, for 40 seconds.
    const auto burning = std::make_shared<MCBattleMech>();
    burning->PartId = 0x1000 + 3 * 0xc80 + 17 * 8 + 5;
    MCWorldStateChunk fire;
    fire.BuildTerrainFire(*burning, 40);
    received = RoundTrip(fire);
    CHECK_EQ(fire.Data, 1u | (5u << 4) | (17u << 7) | (3u << 16) | (40u << 24));
    CHECK(received.EqualTo(fire));
    CHECK_EQ(received.ObjectWid, burning->PartId);

    // OB-104 (kept): only 6 of the seconds' 8 bits are unpacked, so 100 seconds come back as 36.
    fire.BuildTerrainFire(*burning, 100);
    CHECK_EQ(RoundTrip(fire).Param1, 100 - 64);

    // Commander 3's strike of type 6 at cell (100, 200), in 30 seconds; and one landing now (-1).
    MCWorldStateChunk strike;
    strike.Type = MCWorldStateChunk::Artillery + 3;
    strike.TileRow = 100;
    strike.TileCol = 200;
    strike.Param1 = 6;
    strike.Param2 = 30;
    received = RoundTrip(strike);
    CHECK_EQ(strike.Data, 5u | (200u << 4) | (100u << 14) | (31u << 24) | (6u << 29));
    CHECK(received.EqualTo(strike));
    strike.Param2 = -1;
    CHECK_EQ(RoundTrip(strike).Param2, -1);

    MCWorldStateChunk script;
    script.BuildMissionScriptMessage(200, -1234);
    received = RoundTrip(script);
    CHECK_EQ(script.Data, 8u | (200u << 4) | (static_cast<uint32_t>(32000 - 1234) << 12));
    CHECK_EQ(received.Param2, -1234);
    script.BuildMissionScriptMessage(0, 32000);
    CHECK_EQ(RoundTrip(script).Param2, 32000);

    MCWorldStateChunk kill;
    kill.BuildPilotKillStat(17, 4, 24);
    received = RoundTrip(kill);
    CHECK_EQ(kill.Data, 9u | (4u << 4) | (17u << 7));
    CHECK(received.EqualTo(kill));
}

/// <summary>
/// A hit on a mover packs, from the low bit: target type 0 (2 bits), damage in quarter points (10), the roster index
/// (12-16), cause + 7 (17-19), hit location + 2 (20-23), entry angle (24-25) and the refit flag (26).
/// </summary>
TEST_CASE("network: a weapon hit on a mover packs its damage in quarter points and its roster index")
{
    MCTestContextScope scope;
    MCMultiPlayer& game = InstallGame(scope);
    game.NumMovers = 4;

    MCWeaponHitChunk hit = EmptyWeaponHitChunk();
    hit.TargetType = 0;
    hit.TargetId = 3;
    hit.Cause = -2;
    hit.Damage = 12.25f;
    hit.HitLocation = 4;
    hit.EntryAngle = 2;
    CHECK_EQ(game.AddWeaponHitChunk(hit), 1);
    CHECK_EQ(hit.Data, 0u | (49u << 2) | (3u << 12) | (5u << 17) | (6u << 20) | (2u << 24));

    MCWeaponHitChunk received = EmptyWeaponHitChunk();
    received.Data = hit.Data;
    received.Unpack();
    CHECK(received.EqualTo(hit) != 0);
    CHECK_EQ(received.Damage, 12.25f);
}

/// <summary>
/// A hit on anything but a mover goes by the target's class: a building, tree, turret or gate as a terrain object
/// (item at bits 12-14, vertex 15-23, block 24-31), a train car by train (20-27) and car (12-19) with the entry quadrant
/// (28-29), a camera drone as train 0x80; the damage in quarter points sits at bits 2-11 for all of them.
/// </summary>
TEST_CASE("network: a weapon hit on a terrain object, train car or camera drone packs the target by its class")
{
    MCTestContextScope scope;
    InstallGame(scope);
    MCWeaponShotInfo shot;
    shot.Damage = 6.5f;
    shot.EntryAngle = 180.0f;

    const auto building = std::make_shared<MCBattleMech>();
    building->ObjectClass = MCObjectClass::Building;
    building->PartId = 0x1000 + 3 * 0xc80 + 17 * 8 + 5;
    MCWeaponHitChunk hit = EmptyWeaponHitChunk();
    hit.Build(*building, shot, 0);
    hit.Pack();
    CHECK_EQ(hit.Data, 1u | (26u << 2) | (5u << 12) | (17u << 15) | (3u << 24));
    MCWeaponHitChunk received = EmptyWeaponHitChunk();
    received.Data = hit.Data;
    received.Unpack();
    CHECK(received.EqualTo(hit) != 0);
    CHECK_EQ(received.TargetId, building->PartId);

    const auto car = std::make_shared<MCBattleMech>();
    car->ObjectClass = MCObjectClass::TrainCar;
    car->PartId = 0x7d000 + 7 * 100 + 12;
    hit = EmptyWeaponHitChunk();
    hit.Build(*car, shot, 0);
    hit.Pack();
    CHECK_EQ(hit.Data, 2u | (26u << 2) | (12u << 12) | (7u << 20) | (1u << 28));
    received = EmptyWeaponHitChunk();
    received.Data = hit.Data;
    received.Unpack();
    CHECK(received.EqualTo(hit) != 0);
    CHECK_EQ(received.TargetId, car->PartId);

    const auto drone = std::make_shared<MCBattleMech>();
    drone->ObjectClass = MCObjectClass::CameraDrone;
    drone->PartId = 0x802c8 + 3;
    hit = EmptyWeaponHitChunk();
    hit.Build(*drone, shot, 0);
    hit.Pack();
    CHECK_EQ(hit.Data, 2u | (26u << 2) | (3u << 12) | (0x80u << 20) | (1u << 28));
    received = EmptyWeaponHitChunk();
    received.Data = hit.Data;
    received.Unpack();
    CHECK_EQ(received.TargetId, drone->PartId);
    CHECK_EQ(received.Damage, 6.5f);
}

namespace
{
    /// <summary>A weapon fire chunk packed, then unpacked again from its word alone for <paramref name="attacker"/>.</summary>
    MCWeaponFireChunk RoundTrip(MCWeaponFireChunk& chunk, MCBigGameObject& attacker)
    {
        chunk.Pack();
        MCWeaponFireChunk received;
        received.Init();
        received.Data = chunk.Data;
        received.Unpack(attacker);
        return received;
    }
}

/// <summary>
/// A weapon fired packs, from the low bit: the target type (2 bits), the weapon index (5) and the hit flag (1), then
/// the target: a mover's roster index (5 bits), hit location + 2 (4) and entry quadrant (2); a terrain object's item
/// (3), vertex (9) and block (8); a train car's car (8), train (8) and quadrant (2), a camera drone being train 0x80;
/// a map cell's column (10) and row (10). A missile weapon puts its counts below the target, 4 bits each: on a mover
/// the anti-missile shots, the missiles past them and the missiles fired; on anything else the missiles fired alone.
/// The receiver knows the weapon fires missiles from the attacker's own weapon.
/// </summary>
TEST_CASE("network: a weapon fire chunk packs its target, weapon and missiles, and unpacks to the same shot")
{
    MCTestContextScope scope;
    MCMultiPlayer& game = InstallGame(scope);
    game.NumMovers = 8;

    // A turret attacker whose weapon is a laser, then a missile launcher.
    const std::vector<MCMasterComponent> savedComponents = MasterComponentList;
    MasterComponentList.assign(2, MCMasterComponent{});
    MasterComponentList[0].Form = MCComponentForm::WeaponEnergy;
    MasterComponentList[1].Form = MCComponentForm::WeaponMissile;
    const auto turretType = std::make_shared<MCTurretType>();
    turretType->WeaponType = 0;
    const auto attacker = std::make_shared<MCTurret>();
    attacker->ObjectClass = MCObjectClass::Turret;
    attacker->ObjType = turretType;

    // Weapon 7 hits roster mover 5 in location 3, from the right (100 degrees).
    const auto target = std::make_shared<MCBattleMech>();
    target->NetRosterIndex = 5;
    MCWeaponFireChunk fire;
    fire.Init();
    fire.BuildMoverTarget(*target, 7, 1, 100.0f, 0, 0, 0, 3);
    MCWeaponFireChunk received = RoundTrip(fire, *attacker);
    CHECK_EQ(fire.Data, 0u | (7u << 2) | (1u << 7) | (5u << 8) | (5u << 13) | (3u << 17));
    CHECK(received.EqualTo(fire) != 0);

    // A missed shot at terrain object 0x1000 + block 3 * 0xc80 + vertex 17 * 8 + item 5.
    const auto building = std::make_shared<MCBattleMech>();
    building->PartId = 0x1000 + 3 * 0xc80 + 17 * 8 + 5;
    fire.Init();
    fire.BuildTerrainTarget(*building, 2, 0, 0);
    received = RoundTrip(fire, *attacker);
    CHECK_EQ(fire.Data, 1u | (2u << 2) | (5u << 8) | (17u << 11) | (3u << 20));
    CHECK(received.EqualTo(fire) != 0);
    CHECK_EQ(received.TargetId, building->PartId);

    // Car 12 of train 7, from behind; and camera drone 3 (train 0x80), from the left.
    const auto car = std::make_shared<MCBattleMech>();
    car->PartId = 0x7d000 + 7 * 100 + 12;
    fire.Init();
    fire.BuildTrainTarget(*car, 1, 1, 180.0f, 0);
    received = RoundTrip(fire, *attacker);
    CHECK_EQ(fire.Data, 2u | (1u << 2) | (1u << 7) | (12u << 8) | (7u << 16) | (1u << 24));
    CHECK(received.EqualTo(fire) != 0);
    CHECK_EQ(received.TargetId, car->PartId);

    const auto drone = std::make_shared<MCBattleMech>();
    drone->PartId = 0x802c8 + 3;
    fire.Init();
    fire.BuildCameraDroneTarget(*drone, 1, 1, -90.0f, 0);
    received = RoundTrip(fire, *attacker);
    CHECK_EQ(fire.Data, 2u | (1u << 2) | (1u << 7) | (3u << 8) | (0x80u << 16) | (2u << 24));
    CHECK_EQ(received.TargetId, drone->PartId);
    CHECK_EQ(received.EntryAngle, 2);

    // Map cell (row 300, column 700).
    fire.Init();
    fire.TargetType = 3;
    fire.TargetCell = {300, 700};
    fire.WeaponIndex = 4;
    received = RoundTrip(fire, *attacker);
    CHECK_EQ(fire.Data, 3u | (4u << 2) | (700u << 8) | (300u << 18));
    CHECK(received.TargetCell == fire.TargetCell);

    // With the missile launcher: 9 missiles at the mover, 2 shot down by 3 anti-missile shots, from the front.
    turretType->WeaponType = 1;
    fire.Init();
    fire.BuildMoverTarget(*target, 7, 1, 0.0f, 9, 7, 3, 3);
    received = RoundTrip(fire, *attacker);
    CHECK_EQ(fire.Data, 0u | (7u << 2) | (1u << 7) | (3u << 8) | (7u << 12) | (9u << 16) | (5u << 20) | (5u << 25));
    CHECK(received.EqualTo(fire) != 0);

    // At a map cell, only the missiles fired go: all of them count as past the anti-missile systems.
    fire.Init();
    fire.TargetType = 3;
    fire.TargetCell = {300, 700};
    fire.NumMissiles = 6;
    fire.NumMissilesPastAms = 6;
    received = RoundTrip(fire, *attacker);
    CHECK_EQ(fire.Data, 3u | (6u << 8) | (700u << 12) | (300u << 22));
    CHECK_EQ(received.NumMissiles, 6);
    CHECK_EQ(received.NumMissilesPastAms, 6);
    CHECK(received.TargetCell == fire.TargetCell);

    attacker->ObjType = nullptr;
    MasterComponentList = savedComponents;
}

/// <summary>
/// An artillery strike packs into one word: the commander (bits 0-2), the strike type (3-5), the cell's column (6-15)
/// and row (16-25), and the seconds to impact + 1 (26-31), so -1 (the type's own time) goes as 0.
/// </summary>
TEST_CASE("network: an artillery strike packs its commander, type, cell and seconds into one word")
{
    MCArtilleryChunk strike;
    strike.CommanderId = 3;
    strike.StrikeType = 2;
    strike.CellRow = 300;
    strike.CellCol = 700;
    strike.Seconds = 20;
    strike.Pack();
    CHECK_EQ(strike.Data, 3u | (2u << 3) | (700u << 6) | (300u << 16) | (21u << 26));

    MCArtilleryChunk received;
    received.Data = strike.Data;
    received.Unpack();
    CHECK(received.EqualTo(strike) != 0);

    strike.Seconds = -1;
    strike.Pack();
    CHECK_EQ(strike.Data >> 26, 0u);
    received.Data = strike.Data;
    received.Unpack();
    CHECK_EQ(received.Seconds, -1);
    CHECK_EQ(received.CellRow, 300);
}

/// <summary>
/// The server sends a shot's damage in quarter points (rounded down) and its entry angle as a quadrant: front from -45
/// to 45 degrees, left from -135 to -45, right from 45 to 135, the rest the rear; the angle it keeps is the quadrant's
/// middle (0, -90, 90, 180). A client, or a single-player game, keeps both as they are.
/// </summary>
TEST_CASE("network: the server rounds a shot's damage to quarter points and its angle to the quadrant's middle")
{
    const std::array<std::tuple<float, int8_t, float>, 9> angles = {{{0.0f, 0, 0.0f},
                                                                     {45.0f, 0, 0.0f},
                                                                     {-45.0f, 0, 0.0f},
                                                                     {-46.0f, 2, -90.0f},
                                                                     {-134.0f, 2, -90.0f},
                                                                     {46.0f, 3, 90.0f},
                                                                     {134.0f, 3, 90.0f},
                                                                     {135.0f, 1, 180.0f},
                                                                     {-170.0f, 1, 180.0f}}};

    for (const auto& [angle, quadrant, middle] : angles)
    {
        MCTest::Scope scope(std::format("angle {}", angle));
        CHECK_EQ(AngleQuadrant(angle), quadrant);
        CHECK_EQ(SnapAngle(angle), middle);
    }

    CHECK_EQ(QuarterPoints(12.3f), 12.25f);
    CHECK_EQ(QuarterPoints(12.74f), 12.5f);
    CHECK_EQ(QuarterPoints(3.0f), 3.0f);

    MCWeaponShotInfo shot;
    shot.SetDamage(7.6f);
    shot.SetEntryAngle(100.0f);
    CHECK_EQ(shot.Damage, 7.6f);
    CHECK_EQ(shot.EntryAngle, 100.0f);

    MCTestContextScope scope;
    MCMultiPlayer& game = InstallGame(scope);
    shot.SetDamage(7.6f);
    CHECK_EQ(shot.Damage, 7.6f);
    game.IsServer = true;
    shot.SetDamage(7.6f);
    shot.SetEntryAngle(100.0f);
    CHECK_EQ(shot.Damage, 7.5f);
    CHECK_EQ(shot.EntryAngle, 90.0f);
    shot.Init(nullptr, 4, 9.9f, 2, -100.0f);
    CHECK_EQ(shot.Damage, 9.75f);
    CHECK_EQ(shot.EntryAngle, -90.0f);
    CHECK_EQ(shot.MasterId, 4);
    CHECK_EQ(shot.HitLocation, 2);
}

/// <summary>
/// The server's chunks wait in queues without a limit: world events with a tally per type (sent all at once, the
/// tally then cleared), weapon hits handed out a message's worth at a time, oldest first.
/// </summary>
TEST_CASE("network: chunks queue in order, world events tallied by type, hits taken a message's worth at a time")
{
    MCTestContextScope scope;
    MCMultiPlayer& game = InstallGame(scope);
    game.NumMovers = 4;

    for (int32_t i = 0; i < 1100; i++)
    {
        game.AddMineChunk(i % 1000, 7, 0, 1, 0);
    }

    CHECK_EQ(game.AddMissionScriptMessageChunk(3, 9), 1101);
    const auto mover = std::make_shared<FakeMover>();
    mover->NetRosterIndex = 1;
    CHECK_EQ(game.AddPilotKillStat(*mover, 2), 1102);
    CHECK_EQ(game.WorldStateChunkTally[MCWorldStateChunk::Mine], 1100);
    CHECK_EQ(game.WorldStateChunkTally[MCWorldStateChunk::MissionScriptMessage], 1);
    CHECK_EQ(game.WorldStateChunkTally[MCWorldStateChunk::PilotKillStat], 1);
    CHECK_EQ(game.WorldStateChunks[1] >> 14 & 0x3ff, 1u);

    for (int32_t i = 0; i < 3; i++)
    {
        MCWeaponHitChunk hit = EmptyWeaponHitChunk();
        hit.TargetId = i;
        hit.Damage = static_cast<float>(i + 1);
        game.AddWeaponHitChunk(hit);
    }

    const std::vector<uint32_t> queued = game.WeaponHitChunks;
    std::array<uint32_t, 2> first{};
    CHECK_EQ(game.GrabWeaponHitChunks(first), size_t{2});
    CHECK_EQ(first[0], queued[0]);
    CHECK_EQ(first[1], queued[1]);
    CHECK(game.WeaponHitChunks == (std::vector<uint32_t>{queued[2]}));

    // A new game starts with empty queues.
    game.ResetForNewGame();
    CHECK(game.WorldStateChunks.empty());
    CHECK(game.WeaponHitChunks.empty());
}

/// <summary>The multiplayer game is a context system: absent in a single-player game, a scope's own while it lasts.</summary>
TEST_CASE("network: the multiplayer game is the context's, and gone with it")
{
    CHECK(MultiPlayer() == nullptr);

    {
        MCTestContextScope scope;
        MCMultiPlayer& game = InstallGame(scope);
        CHECK(MultiPlayer().get() == &game);
        // A new game: nobody checked in, no team, no roles yet.
        CHECK_EQ(game.CheckInId, -1);
        CHECK_EQ(game.HomeTeam, -1);
        CHECK(!game.IsServer);
        CHECK_EQ(NumLanPlayers, 1);
        CHECK(game.SessionManager == nullptr);
        scope.Context().SetMultiPlayer(nullptr);
        CHECK(MultiPlayer() == nullptr);
    }

    CHECK(MultiPlayer() == nullptr);
}

/// <summary>
/// A message is built over the last one in the buffer: the original wrote only the fields it set, so the others keep the
/// earlier message's bytes, and the header is the new type's.
/// </summary>
TEST_CASE("network: a message built in the buffer keeps the bytes of the fields it doesn't set")
{
    MCTestContextScope scope;
    MCMultiPlayer& game = InstallGame(scope);
    game.MsgBuffer.fill(0xab);
    auto start = game.StartGuaranteedMessage<MCMPStartScenarioMessage>(MCMPMessageType::StartScenario);
    CHECK_EQ(start.Header, GuaranteedHeader(MCMPMessageType::StartScenario));
    CHECK_EQ(start.PlayerValues[5], static_cast<int32_t>(0xabababab));
    CHECK_EQ(start.MoverFlags[23], 0xab);
    start.PlayerValues[0] = 7;
    game.PutMessage(start);
    CHECK_EQ(ReadLinkupMessage<MCMPStartScenarioMessage>(game.Built(sizeof(start))).PlayerValues[0], 7);
    CHECK_EQ(ReadLinkupMessage<MCMPStartScenarioMessage>(game.Built(sizeof(start))).PlayerValues[1],
             static_cast<int32_t>(0xabababab));
    // Past the layout the buffer is untouched (a mission name goes there).
    CHECK_EQ(game.MsgBuffer[sizeof(start)], 0xab);
    // A plain message sets only its header word.
    game.MsgBuffer.fill(0xab);
    game.StartPlainMessage(MCMPMessageType::MoverUpdate);
    CHECK_EQ(LinkupHeaderOf(game.Built(2)), PlainHeader(MCMPMessageType::MoverUpdate));
    CHECK_EQ(game.MsgBuffer[2], 0xab);
}

/// <summary>
/// A client applies the server's mover update: each rostered mover gets its move chunk, its status chunk with the
/// update's age, and its pilot the id of the order it runs. An update older than the last one is dropped.
/// </summary>
TEST_CASE("network: a client hands each mover its move and status chunks and drops an older update")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    const auto first = std::make_shared<FakeMover>();
    const auto second = std::make_shared<FakeMover>();
    game.AddToMoverRoster(first);
    game.AddToMoverRoster(second);
    CHECK_EQ(second->NetRosterIndex, 1);
    game.MoverUpdateSequence = 4;

    // Update 6: two move chunks, two status chunks, two order ids.
    std::vector<uint8_t> update = Message(MCMPMessageType::MoverUpdate, {6, 0});
    PutWord(update, 0x11111111);
    PutWord(update, 0x22222222);
    PutWord(update, 0x33333333);
    PutWord(update, 0x44444444);
    update.push_back(9);
    update.push_back(12);
    HandleAppMoverUpdate(1, update);
    CHECK_CALLED_WITH(first->MoveChunks, 0x11111111u);
    CHECK_CALLED_WITH(second->MoveChunks, 0x22222222u);
    // Two updates (4 and 5) were missed: the status is two updates old.
    CHECK_CALLED_WITH(first->StatusChunks, 2, 0x33333333u);
    CHECK_CALLED_WITH(second->StatusChunks, 2, 0x44444444u);
    CHECK_EQ(first->FakePilot->LastTacOrderId, 9);
    CHECK_EQ(second->FakePilot->LastTacOrderId, 12);
    CHECK_EQ(int{game.MoverUpdateSequence}, 7);

    // Update 5 arrives late: nothing happens.
    update[2] = 5;
    HandleAppMoverUpdate(1, update);
    CHECK_CALLED(first->MoveChunks, 1);
    CHECK_EQ(int{game.MoverUpdateSequence}, 7);

    // The server sends these; it doesn't apply them, but counts on from them.
    game.IsServer = true;
    update[2] = 8;
    HandleAppMoverUpdate(1, update);
    CHECK_CALLED(first->MoveChunks, 1);
    CHECK_EQ(int{game.MoverUpdateSequence}, 9);
}

/// <summary>
/// A client points each rostered turret at the mover the server's turret update names by roster index; 0xff is no
/// target. Outside a mission with company, the update is ignored.
/// </summary>
TEST_CASE("network: a client aims each turret at the rostered mover the server names")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    const auto target = std::make_shared<FakeMover>();
    game.AddToMoverRoster(target);
    const auto aiming = std::make_shared<MCTurret>();
    const auto idle = std::make_shared<MCTurret>();
    game.AddToTurretRoster(aiming);
    game.AddToTurretRoster(idle);
    CHECK_EQ(idle->NetRosterIndex, 1);
    idle->Target = target;

    HandleAppTurretUpdate(1, Message(MCMPMessageType::TurretUpdate, {0, 0, 0, 0xff}));
    CHECK(LockObject(aiming->Target) == target);
    CHECK(LockObject(idle->Target) == nullptr);
    CHECK_EQ(int{game.TurretUpdateSequence}, 1);

    // Alone in the session (the other player left), the game is no longer synchronised.
    NumLanPlayers = 1;
    HandleAppTurretUpdate(1, Message(MCMPMessageType::TurretUpdate, {1, 0, 0xff, 0}));
    CHECK(LockObject(aiming->Target) == target);
    CHECK_EQ(int{game.TurretUpdateSequence}, 1);
}

/// <summary>The session messages a client keeps: the planning has started, and the scenario's result.</summary>
TEST_CASE("network: the start-planning and end-scenario messages set the game's state")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    game.InMission = false;
    HandleAppStartPlanning(1, Message(MCMPMessageType::StartPlanning, {0, 0, 0, 0, 0, 0}));
    CHECK(game.InLogistics);

    std::vector<uint8_t> end = Message(MCMPMessageType::EndScenario, {0, 0, 0, 0, 0, 0});
    PutWord(end, 2);
    HandleAppEndScenario(1, end);
    CHECK_EQ(game.ScenarioResult, 2);
}

namespace
{
    /// <summary>Hands <paramref name="bytes"/> to the game as a message from player <paramref name="fromID"/>.</summary>
    void Deliver(uint32_t fromID, std::span<const uint8_t> bytes)
    {
        MCFidpMessage msg(0, 0x200);
        msg.FromID = fromID;
        msg.SetMessageBuffer(bytes);
        MultiPlayerApplicationCallback(msg);
    }
}

/// <summary>
/// What runs the game (here the planning's start and the scenario's end) is taken only from the server or the session's
/// host; from any other player it is dropped (issue #25: the original took it from anyone).
/// </summary>
TEST_CASE("network: the server's and host's messages are dropped from anyone else")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    game.InMission = false;
    game.ServerID = 7;
    game.HostID = 8;

    std::vector<uint8_t> end = Message(MCMPMessageType::EndScenario, {0, 0, 0, 0, 0, 0});
    PutWord(end, 3);
    Deliver(9, end);
    CHECK_EQ(game.ScenarioResult, 0);
    Deliver(7, end);
    CHECK_EQ(game.ScenarioResult, 3);
    end[8] = 4;
    Deliver(8, end);
    CHECK_EQ(game.ScenarioResult, 4);

    const std::vector<uint8_t> planning = Message(MCMPMessageType::StartPlanning, {0, 0, 0, 0, 0, 0});
    Deliver(9, planning);
    CHECK(!game.InLogistics);
    Deliver(8, planning);
    CHECK(game.InLogistics);
}

/// <summary>
/// The server's updates name movers, turrets and targets by index, and events by type; a client skips any it doesn't
/// have (issue #25: the original read and wrote past its rosters, or was fatal), and keeps the ones it does.
/// </summary>
TEST_CASE("network: a client skips update entries that name movers, turrets or events it doesn't have")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    const auto mover = std::make_shared<FakeMover>();
    game.AddToMoverRoster(mover);
    const auto turret = std::make_shared<MCTurret>();
    game.AddToTurretRoster(turret);
    turret->Target = mover;

    // A turret aimed at roster index 30 (of one) has no target.
    HandleAppTurretUpdate(1, Message(MCMPMessageType::TurretUpdate, {0, 0, 30}));
    CHECK(LockObject(turret->Target) == nullptr);
    HandleAppTurretUpdate(1, Message(MCMPMessageType::TurretUpdate, {1, 0, 0}));
    CHECK(LockObject(turret->Target) == mover);

    // Weapon fire and critical hits for movers 20 and 21, and turret 40: nobody gets them.
    std::vector<uint8_t> fire = Message(MCMPMessageType::MoverWeaponFireUpdate, {0, 0, 0, 0, 0, 0, 20, 2, 1, 1});
    fire.resize(0x22);
    PutWord(fire, 0x12345678);
    PutWord(fire, 0x12345678);
    HandleAppMoverWeaponFireUpdate(1, fire);
    CHECK_EQ(mover->NumWeaponFireChunks[1], 0);
    HandleAppTurretWeaponFireUpdate(
        1, Message(MCMPMessageType::TurretWeaponFireUpdate, {0, 0, 0, 0, 0, 0, 1, 40 << 2 | 1, 0, 0, 0, 0}));
    CHECK(turret->WeaponFireChunks[1].empty());
    std::vector<uint8_t> critical = Message(MCMPMessageType::MoverCriticalHitUpdate, {0, 0, 0, 0, 0, 0, 100, 1, 1});
    critical.resize(0x3b, 0x11);
    HandleAppMoverCriticalHitUpdate(1, critical);
    CHECK_EQ(mover->NumCriticalHitChunks[1], 0);

    // Weapon hits on roster mover 31 and of target type 3 hit nothing.
    std::vector<uint8_t> hits = Message(MCMPMessageType::WeaponHitUpdate, {0, 0, 0, 0, 0, 0, 2});
    PutWord(hits, 31u << 12 | 4u << 2);
    PutWord(hits, 3u | 4u << 2);
    HandleAppWeaponHitUpdate(1, hits);
    CHECK_EQ(mover->TotalDamageTaken, 0.0f);

    // A world event of a type the game doesn't have, and a kill of kind 7 (the pilot counts six kinds and the
    // elementals), change nothing.
    std::vector<uint8_t> world = Message(MCMPMessageType::WorldStateUpdate, {0, 0, 0, 0, 0, 0, 2, 0});
    PutWord(world, 12);
    PutWord(world, MCWorldStateChunk::PilotKillStat | 7u << 4);
    HandleAppWorldStateUpdate(1, world);

    for (const auto& kills : mover->FakePilot->NumKilled)
    {
        CHECK_EQ(kills[1], 0);
    }

    // The mover the server does have still gets its fire.
    fire[8] = 0;
    fire[9] = 1;
    HandleAppMoverWeaponFireUpdate(1, fire);
    CHECK_EQ(mover->NumWeaponFireChunks[1], 1);
}

/// <summary>
/// The server's fire spreading to a forest tile, a bridge or a wall (a misc terrain object) gives that object's fire
/// more burn time on each client. Clients took the object for a terrain object, another class, and crashed.
/// </summary>
TEST_CASE("network: a client lights the forest tile the server's fire spread to")
{
    ClientGame client;
    client.Scope.Context().SetObjectSystem(std::make_shared<MCObjectSystem>(std::make_unique<MCObjectTypeManager>()));
    client.Scope.Context().SetEffectSystem(std::make_unique<MCEffectSystem>(4, 30.0f));

    // A forest tile in block 3 (vertex 17, item 5), burning with a second left.
    const auto forest = std::make_shared<MCMiscTerrainObject>();
    forest->ObjType = std::make_shared<MCMiscTerrainObjectType>();
    forest->ObjectClass = MCObjectClass::MiscTerrainObject;
    forest->PartId = 0x1000 + 3 * 0xc80 + 17 * 8 + 5;
    const std::shared_ptr<MCObjectList> block = ObjectList()->FindOrAddList("RBlk3");
    block->Add(forest);
    const auto fire = std::make_shared<MCFire>();
    fire->Flames.resize(1);
    fire->Flames[0].TimeLeftToBurn = 1.0f;
    forest->FireObject.Light(fire);

    MCWorldStateChunk spread;
    spread.BuildTerrainFire(*forest, 10);
    spread.Pack();
    std::vector<uint8_t> world = Message(MCMPMessageType::WorldStateUpdate, {0, 0, 0, 0, 0, 0, 1, 0});
    PutWord(world, spread.Data);
    HandleAppWorldStateUpdate(1, world);
    CHECK_EQ(fire->Flames[0].TimeLeftToBurn, 11.0f);

    CHECK(block->Remove(*forest));
}

/// <summary>
/// A critical hit the server sends names a body location and critical space; one on an empty space (item 0xff) or a
/// location the mech doesn't have is skipped (issue #25: the original hit inventory item -1).
/// </summary>
TEST_CASE("network: a critical hit on an empty critical space hits no item")
{
    ClientGame client;
    const auto mech = std::make_shared<FakeMover>();
    mech->Body.resize(1);
    mech->Body[0].CriticalSpaces.resize(2);
    mech->Body[0].CriticalSpaces[0].InventoryID = 0xff;
    mech->Body[0].CriticalSpaces[1].InventoryID = 3;
    const std::array<uint8_t, 4> chunks{0x00, 0x01, 0x05, 0x70};
    mech->AddCriticalHitChunks(1, chunks);
    CHECK_EQ(mech->NumCriticalHitChunks[1], 4);
    mech->UpdateCriticalHitChunks(1);
    CHECK_EQ(mech->NumCriticalHitChunks[1], 0);
    CHECK(mech->Inventory.empty());
    CHECK_EQ(mech->Body[0].DamageState, 0);
}

/// <summary>
/// A player checks in only for itself: a check-in naming a check-in id the sender doesn't have is dropped (issue #25:
/// the original wrote any id 0-5, or past the table, into the server's check-in list).
/// </summary>
TEST_CASE("network: a check-in for another player is dropped")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    game.IsServer = true;
    game.InMission = false;
    std::vector<uint8_t> checkIn = Message(MCMPMessageType::PlayerCheckIn, {0, 0, 0, 0, 0, 0, 1, 0});
    HandleAppPlayerCheckIn(5, checkIn);
    checkIn[8] = 0x7f;
    HandleAppPlayerCheckIn(5, checkIn);
    CHECK(std::ranges::all_of(game.PlayerCheckedIn, [](int32_t id) { return id == 0; }));
}
