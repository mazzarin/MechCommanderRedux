#include "stdafx.h"
#include "MCTest.h"
#include "MCMock.h"
#include "TestGame.h"
#include "ai/MCScenarioMap.h"
#include "fakes/MCMemoryFileSource.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "linkup/MCFidpMessage.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "mission/MCScenario.h"
#include "mission/MCWeatherPlay.h"
#include "network/MCGameDataHash.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCommander.h"
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
#include "sprite/MCMechStops.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"
// The editor's dusk and night sight (header-only), for the game's own to be held to.
#include "../../MCEditor/MissionTimeOfDay.h"

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
/// The server sends the next mover update early once a mover whose last chunk said its move goes on past it has more of
/// it planned: its next path or leg, or its wait over (its paths changed since). A chunk that said the move ends there,
/// or a mover whose paths stand as they were, waits for the update's time.
/// </summary>
TEST_CASE("network: the server sends a mover's next cells early once the move it said goes on has more planned")
{
    ClientGame server;
    MCMultiPlayer& game = *server.Game;
    game.IsServer = true;
    const auto mover = std::make_shared<FakeMover>();
    game.AddToMoverRoster(mover);
    MCMechWarrior& pilot = *mover->FakePilot;
    CHECK(!game.MoverCellsAwaited());

    mover->MoveChunk.Word = MCChunkWord::GoesOn;
    mover->MoveChunk.Paths = MCChunkPaths::Of(pilot);
    CHECK(!game.MoverCellsAwaited());

    // The next leg planned.
    constexpr int32_t LegSteps = 5;
    pilot.MoveOrders.Path[1]->NumStepsWhenNotPaused = LegSteps;
    CHECK(game.MoverCellsAwaited());

    // Sent: the chunk it gets now is built from the paths as they stand.
    mover->MoveChunk.Paths = MCChunkPaths::Of(pilot);
    CHECK(!game.MoverCellsAwaited());

    // A wait over: the path paused, then walked on.
    pilot.MoveOrders.Path[0]->NumStepsWhenNotPaused = LegSteps;
    mover->MoveChunk.Paths = MCChunkPaths::Of(pilot);
    pilot.MoveOrders.Path[0]->NumSteps = LegSteps;
    CHECK(game.MoverCellsAwaited());

    // A chunk that said the move ends there is no reason to send early.
    mover->MoveChunk.Word = MCChunkWord::EndsHere;
    CHECK(!game.MoverCellsAwaited());
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
    {
        // The original's assert on the radio count (0x11 here) still reports.
        MCTest::ExpectAssert badRadio("bad numRDO", 1);
        HandleAppMoverCriticalHitUpdate(1, critical);
    }

    CHECK_EQ(mover->NumCriticalHitChunks[1], 0);

    // Weapon hits on roster mover 31 and of target type 3 hit nothing.
    std::vector<uint8_t> hits = Message(MCMPMessageType::WeaponHitUpdate, {0, 0, 0, 0, 0, 0, 2});
    PutWord(hits, 31u << 12 | 4u << 2);
    PutWord(hits, 3u | 4u << 2);
    {
        // The original's asserts on the unpacked hits still report.
        MCTest::ExpectAssert badTarget("bad targetId", 1);
        MCTest::ExpectAssert badLocation("bad hitLocation", 1);
        HandleAppWeaponHitUpdate(1, hits);
    }

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

/// <summary>
/// The match plays by the server's rules beyond the original (MechBraking) that every player's build plays by: a
/// player's rules message is kept by its id; the match's is taken only from the server or host, keeping the rules this
/// build plays by, and a short one is dropped; in a match mechs brake by its rules alone, whatever the player's own
/// MechBraking. The rules agreed: those wanted that every player said it plays by, none with a player that said none
/// (a build without them).
/// </summary>
TEST_CASE("network: a match plays by the server's rules that every player's build plays by")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    game.ServerID = 7;
    game.HostID = 8;
    GMechBraking = 1;
    const auto rulesMessage = [](MCMPMessageType type, uint32_t rules)
    {
        std::vector<uint8_t> message = Message(type, {0, 0, 0, 0, 0, 0});
        PutWord(message, rules);
        return message;
    };

    Deliver(9, rulesMessage(MCMPMessageType::PlayerRules, MCMatchRule::MechBraking));
    CHECK_EQ(game.PlayerRules.at(9), MCMatchRule::MechBraking);
    CHECK(!MCMechBrakingInForce());

    const std::vector<uint8_t> every = rulesMessage(MCMPMessageType::MatchRules, 0xffffffff);
    Deliver(9, every);
    CHECK_EQ(game.MatchRules, 0u);
    Deliver(7, every);
    CHECK_EQ(game.MatchRules, MCMatchRule::Supported);
    CHECK(MCMechBrakingInForce());
    GMechBraking = 0;
    CHECK(MCMechBrakingInForce());

    std::vector<uint8_t> none = rulesMessage(MCMPMessageType::MatchRules, 0);
    none.pop_back();
    Deliver(8, none);
    CHECK_EQ(game.MatchRules, MCMatchRule::Supported);
    Deliver(8, rulesMessage(MCMPMessageType::MatchRules, 0));
    CHECK(!MCMechBrakingInForce());

    const std::map<uint32_t, uint32_t> said = {{1, MCMatchRule::MechBraking}, {2, MCMatchRule::MechBraking}};
    const std::array<uint32_t, 2> both = {1, 2};
    const std::array<uint32_t, 3> withOld = {1, 2, 3};
    CHECK_EQ(MCAgreedRules(MCMatchRule::MechBraking, both, said), MCMatchRule::MechBraking);
    CHECK_EQ(MCAgreedRules(0, both, said), 0u);
    CHECK_EQ(MCAgreedRules(MCMatchRule::MechBraking, withOld, said), 0u);
}

/// <summary>
/// Port: what a match plays of its host's match options (the session screen's MATCH OPTIONS) is taken only from the
/// host or the server, kept as far as this build knows it (a weather or time of day it doesn't is the mission's own;
/// rules it doesn't play by are left out), and a short message is dropped. The rules the server wants are the host's,
/// else its own PREFS.
/// </summary>
TEST_CASE("network: a guest keeps what the match plays of the host's options as far as its build knows them")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    game.ServerID = 7;
    game.HostID = 8;
    const auto optionsMessage = [](int8_t weather, int8_t time, uint32_t rules)
    {
        std::vector<uint8_t> message = Message(MCMPMessageType::MatchOptions, {0, 0, 0, 0, 0, 0});
        message.push_back(static_cast<uint8_t>(weather));
        message.push_back(static_cast<uint8_t>(time));
        PutWord(message, rules);
        return message;
    };

    const std::vector<uint8_t> snowyNight = optionsMessage(
        static_cast<int8_t>(MCWeatherKind::Snow), static_cast<int8_t>(MCTimeOfDay::Night), MCMatchRule::MechBraking);
    Deliver(9, snowyNight);
    CHECK(game.Played == MCMatchOptions{});

    Deliver(8, snowyNight);
    CHECK(game.Played.Weather == MCWeatherKind::Snow);
    CHECK(game.Played.TimeOfDay == MCTimeOfDay::Night);
    CHECK(game.Played.Rules == MCMatchRule::MechBraking);
    CHECK(game.MatchOptions == MCMatchOptions{});

    std::vector<uint8_t> shortMessage = optionsMessage(static_cast<int8_t>(MCWeatherKind::Rain), 0, 0);
    shortMessage.pop_back();
    Deliver(7, shortMessage);
    CHECK(game.Played.Weather == MCWeatherKind::Snow);

    // A later build's weather and time of day, and every rule.
    constexpr int8_t Unknown = 99;
    Deliver(7, optionsMessage(Unknown, Unknown, 0xffffffff));
    CHECK(!game.Played.Weather.has_value());
    CHECK(!game.Played.TimeOfDay.has_value());
    CHECK(game.Played.Rules == MCMatchRule::Supported);

    Deliver(8, optionsMessage(MCMPMatchOptionsMessage::MissionOwn, MCMPMatchOptionsMessage::MissionOwn, 0));
    CHECK(!game.Played.Weather.has_value());
    CHECK(!game.Played.TimeOfDay.has_value());
    CHECK(game.Played.Rules == 0u);

    // The host's choice, else the server's own PREFS.
    game.MatchOptions.Rules = MCMatchRule::MechBraking;
    CHECK_EQ(game.WantedRules(), MCMatchRule::MechBraking);
    game.MatchOptions.Rules.reset();
    GMechBraking = 1;
    CHECK_EQ(game.WantedRules(), MCMatchRule::MechBraking);
    GMechBraking = 0;
    CHECK_EQ(game.WantedRules(), 0u);
}

/// <summary>
/// Port: a match plays its host's weather, time of day and rules only as far as every player's build plays them, as its
/// rules message said: the host's time of day brings its sight's rule with it, and needs both from every build; the
/// weather's play comes with every build playing it (for the mission's own weather too), and the host's weather needs
/// its look and its play from every build. A build of the time of day's sight (3 Nov) keeps the mission's own weather,
/// one of the first match options (the looks alone) the mission's own time of day too, one of the braking's round
/// (braking alone) both, one that said none (the original's) every option. The players without a rule are those whose
/// message left it out or who sent none.
/// </summary>
TEST_CASE("network: a match plays the host's options as far as every player's build plays them")
{
    constexpr uint32_t braking = MCMatchRule::MechBraking;
    constexpr uint32_t timeRules = MCMatchRule::TimeOfDayRules;
    constexpr uint32_t weatherPlay = MCMatchRule::WeatherPlay;
    constexpr uint32_t firstOptions = braking | MCMatchRule::MatchWeather | MCMatchRule::MatchTimeOfDay;
    constexpr uint32_t sightOptions = braking | MCMatchRule::MatchWeather | timeRules;
    const MCMatchOptions chosen{.Weather = MCWeatherKind::Snow, .TimeOfDay = MCTimeOfDay::Night, .Rules = braking};
    CHECK((MCPlayedOptions(chosen, braking, MCMatchRule::Supported) ==
           MCMatchOptions{.Weather = MCWeatherKind::Snow,
                          .TimeOfDay = MCTimeOfDay::Night,
                          .Rules = braking | timeRules | weatherPlay}));
    CHECK((MCPlayedOptions(chosen, 0, MCMatchRule::Supported) == MCMatchOptions{.Weather = MCWeatherKind::Snow,
                                                                                .TimeOfDay = MCTimeOfDay::Night,
                                                                                .Rules = timeRules | weatherPlay}));
    CHECK((MCPlayedOptions(MCMatchOptions{}, braking | timeRules, MCMatchRule::Supported) ==
           MCMatchOptions{.Rules = braking | weatherPlay}));
    CHECK((MCPlayedOptions(chosen, braking, sightOptions) ==
           MCMatchOptions{.TimeOfDay = MCTimeOfDay::Night, .Rules = braking | timeRules}));
    CHECK(MCPlayedOptions(chosen, braking, firstOptions) == MCMatchOptions{.Rules = braking});
    CHECK(MCPlayedOptions(chosen, braking, braking) == MCMatchOptions{.Rules = braking});
    CHECK(MCPlayedOptions(chosen, braking, MCMatchRule::MatchWeather) == MCMatchOptions{.Rules = 0u});
    constexpr uint32_t bans =
        MCMatchRule::NoArtillery | MCMatchRule::NoProbes | MCMatchRule::NoSensors | MCMatchRule::NoMines;
    CHECK(MCPlayedOptions(MCMatchOptions{}, bans, MCMatchRule::Supported) ==
          MCMatchOptions{.Rules = bans | weatherPlay});
    CHECK(MCPlayedOptions(MCMatchOptions{}, bans, sightOptions | weatherPlay | MCMatchRule::NoMines) ==
          MCMatchOptions{.Rules = MCMatchRule::NoMines | weatherPlay});
    CHECK(MCPlayedOptions(chosen, braking, 0) == MCMatchOptions{.Rules = 0u});

    const std::map<uint32_t, uint32_t> said = {
        {1, MCMatchRule::Supported}, {2, braking}, {4, firstOptions}, {5, sightOptions}};
    const std::array<uint32_t, 5> players = {1, 2, 3, 4, 5};
    CHECK((MCPlayersWithout(MCMatchRule::MatchWeather, players, said) == std::vector<uint32_t>{2, 3}));
    CHECK((MCPlayersWithout(MCMatchRule::WeatherRules, players, said) == std::vector<uint32_t>{2, 3, 4, 5}));
    CHECK((MCPlayersWithout(timeRules, players, said) == std::vector<uint32_t>{2, 3, 4}));
    CHECK(MCPlayersWithout(braking, players, said) == std::vector<uint32_t>{3});
    CHECK(MCPlayersWithout(braking, std::span(players).first(2), said).empty());
}

/// <summary>
/// Port: at a match's time of day units see as far as in a mission the editor set then (each level's sight cut to the
/// same share, rounded the same, the same least), from the game system's table, which is the editor's by day; only when
/// the match plays its sight's rule (every player's game has it): else, or with the mission's own time of day, the
/// mission's own table.
/// </summary>
TEST_CASE("network: a match's time of day sets how far units see when every player's game plays it")
{
    std::array<int32_t, VisualRangeTableSize> day{};

    for (int32_t level = 0; level < VisualRangeTableSize; level++)
    {
        day[static_cast<size_t>(level)] = MissionVisualRange(level, MissionTimeOfDay::Day);
    }

    // The retail game system's, where there is a game.
    if (MCTestGame::Available())
    {
        MCTestGame::OpenFastFiles();
        MCFitIniFile gameSystem;
        REQUIRE_EQ(gameSystem.Open(GamePath(MissionPath, "gamesys", ".fit")), 0);
        REQUIRE_EQ(gameSystem.SeekBlock("General"), 0);
        std::array<int32_t, VisualRangeTableSize> retail{};
        REQUIRE(gameSystem.ReadArray<int32_t>("VisualRangeTable", retail).has_value());
        CHECK(retail == day);
    }

    CHECK(MCSightAt(day, MCTimeOfDay::Day) == day);
    const std::array<int32_t, VisualRangeTableSize> dusk = MCSightAt(day, MCTimeOfDay::Dusk);
    const std::array<int32_t, VisualRangeTableSize> night = MCSightAt(day, MCTimeOfDay::Night);

    for (int32_t level = 0; level < VisualRangeTableSize; level++)
    {
        const auto at = static_cast<size_t>(level);
        CHECK_EQ(dusk[at], MissionVisualRange(level, MissionTimeOfDay::Dusk));
        CHECK_EQ(night[at], MissionVisualRange(level, MissionTimeOfDay::Night));
    }

    const MCMatchOptions atNight =
        MCPlayedOptions(MCMatchOptions{.TimeOfDay = MCTimeOfDay::Night}, 0, MCMatchRule::Supported);
    CHECK(MCMatchSight(atNight, day) == night);
    CHECK(!MCMatchSight(MCMatchOptions{.TimeOfDay = MCTimeOfDay::Night, .Rules = MCMatchRule::MatchTimeOfDay}, day));
    CHECK(!MCMatchSight(MCMatchOptions{.Rules = MCMatchRule::TimeOfDayRules}, day));
    CHECK(!MCMatchSight(MCMatchOptions{}, day));
}

/// <summary>
/// Port: a match's weather changes play (MCWeatherPlay) only when the match plays the weather's play (every player's
/// game has it), the host's weather else the mission's own; clear weather changes nothing. Every weather damps fires
/// and makes a fall likelier, none speeds a vehicle or lengthens sight; a downpour and a blizzard cost long-range aim
/// and sight. Sight is cut as the time of day cuts it.
/// </summary>
TEST_CASE("network: a match's weather changes play when every player's game plays it, clear weather nothing")
{
    CHECK(MCWeatherPlayOf(MCWeatherKind::Clear) == MCWeatherPlay{});

    for (const MCWeatherKind kind : {MCWeatherKind::Rain, MCWeatherKind::Downpour, MCWeatherKind::LightSnow,
                                     MCWeatherKind::Snow, MCWeatherKind::Blizzard})
    {
        const MCWeatherPlay play = MCWeatherPlayOf(kind);
        CHECK(play.Burn > 0.0f);
        CHECK(play.Burn < 1.0f);
        CHECK(play.Spread >= 0);
        CHECK(play.Spread < MCWeatherPlay{}.Spread);
        CHECK(play.Footing > 0.0);
        CHECK(play.LongAim <= 0.0f);
        CHECK(play.Sight > 0.0);
        CHECK(play.Sight <= 1.0);
        CHECK(play.VehicleSpeed > 0.0f);
        CHECK(play.VehicleSpeed <= 1.0f);
    }

    for (const MCWeatherKind kind : {MCWeatherKind::Downpour, MCWeatherKind::Blizzard})
    {
        CHECK(MCWeatherPlayOf(kind).LongAim < 0.0f);
        CHECK(MCWeatherPlayOf(kind).Sight < 1.0);
    }

    constexpr uint32_t weatherPlay = MCMatchRule::WeatherPlay;
    const MCWeatherPlay blizzard = MCWeatherPlayOf(MCWeatherKind::Blizzard);
    CHECK(MCMatchWeatherPlay(MCMatchOptions{.Weather = MCWeatherKind::Blizzard}, MCWeatherKind::Rain) ==
          MCWeatherPlay{});
    CHECK(MCMatchWeatherPlay(MCMatchOptions{.Weather = MCWeatherKind::Blizzard, .Rules = weatherPlay},
                             MCWeatherKind::Rain) == blizzard);
    CHECK(MCMatchWeatherPlay(MCMatchOptions{.Rules = weatherPlay}, MCWeatherKind::Rain) ==
          MCWeatherPlayOf(MCWeatherKind::Rain));
    CHECK(MCMatchWeatherPlay(MCMatchOptions{.Rules = weatherPlay}, MCWeatherKind::Clear) == MCWeatherPlay{});
    CHECK(MCMatchWeatherPlay(MCMatchOptions{}, MCWeatherKind::Snow) == MCWeatherPlay{});

    std::array<int32_t, VisualRangeTableSize> table{};
    table.fill(10);
    table[0] = 5;
    table[1] = 1;
    const std::array<int32_t, VisualRangeTableSize> cut = MCSightCut(table, 0.7);
    CHECK_EQ(cut[0], 4);
    CHECK_EQ(cut[1], LeastSight);
    CHECK_EQ(cut[2], 7);
    CHECK(MCSightCut(table, 1.0) == table);
    CHECK(MCSightAt(table, MCTimeOfDay::Night) == MCSightCut(table, NightSight));
}

/// <summary>
/// Port: a match without artillery gives commanders no artillery strikes, one without probes no sensor probes or
/// camera drones (whatever the mission or its script gives; one without sensors still does), and one without mines
/// clears the map's (each side's laid and gone-off mines, and what each knows of them), the tiles' other bits kept;
/// outside such a match, as given.
/// </summary>
TEST_CASE("network: a match without artillery, probes or mines gives none, and clears the map's mines")
{
    MCTestContextScope scope;
    MCCommander commander(0);
    const auto give = [&commander]
    {
        commander.SetNumSmallStrikes(3);
        commander.SetNumLargeStrikes(2);
        commander.SetNumSensorStrikes(4);
        commander.SetNumCameraDrones(1);
        return std::array{commander.NumSmallStrikes, commander.NumLargeStrikes, commander.NumSensorStrikes,
                          commander.NumCameraDrones};
    };

    CHECK((give() == std::array{3, 2, 4, 1}));
    MCSetMatchRulesInForce(MCMatchRule::NoArtillery);
    CHECK(MCMatchPlays(MCMatchRule::NoArtillery));
    CHECK(!MCMatchPlays(MCMatchRule::NoArtillery | MCMatchRule::NoProbes));
    CHECK((give() == std::array{0, 0, 4, 1}));
    MCSetMatchRulesInForce(MCMatchRule::NoProbes);
    CHECK((give() == std::array{3, 2, 0, 0}));
    MCSetMatchRulesInForce(MCMatchRule::NoSensors);
    CHECK((give() == std::array{3, 2, 4, 1}));
    MCSetMatchRulesInForce(0);
    CHECK((give() == std::array{3, 2, 4, 1}));

    MCScenarioMap map(2, 1);
    map.TileAt(0, 0).Overlay = 0xffffffffu;
    map.TileAt(0, 1).Overlay = 0x00000801u;
    map.ClearMines();
    CHECK_EQ(map.TileAt(0, 0).Overlay, 0xe1ff87ffu);
    CHECK_EQ(map.TileAt(0, 1).Overlay, 0x00000001u);
}

/// <summary>
/// Port: the host's word for every player's rules (MCMPPlayersRulesMessage) is a guest's, for its MATCH OPTIONS: taken
/// only from the server or host, the bits this build doesn't play left out, a short one dropped.
/// </summary>
TEST_CASE("network: a guest keeps the host's word for every player's rules")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    game.ServerID = 7;
    game.HostID = 7;
    game.PlayerRules = {{9, MCMatchRule::Supported}};
    const auto playersMessage = [](std::initializer_list<std::pair<uint32_t, uint32_t>> players)
    {
        std::vector<uint8_t> message =
            Message(MCMPMessageType::PlayersRules, {0, 0, 0, 0, 0, 0, static_cast<uint8_t>(players.size())});

        for (const auto& [id, rules] : players)
        {
            PutWord(message, id);
            PutWord(message, rules);
        }

        message.resize(sizeof(MCMPPlayersRulesMessage), 0);
        return message;
    };

    const std::vector<uint8_t> heard = playersMessage({{7, 0xffffffff}, {8, MCMatchRule::MechBraking}});
    Deliver(8, heard);
    CHECK((game.PlayerRules == std::map<uint32_t, uint32_t>{{9, MCMatchRule::Supported}}));
    Deliver(7, heard);
    CHECK(
        (game.PlayerRules == std::map<uint32_t, uint32_t>{{7, MCMatchRule::Supported}, {8, MCMatchRule::MechBraking}}));

    std::vector<uint8_t> shortMessage = playersMessage({});
    shortMessage.pop_back();
    Deliver(7, shortMessage);
    CHECK_EQ(game.PlayerRules.size(), 2u);
    Deliver(7, playersMessage({}));
    CHECK(game.PlayerRules.empty());
}

/// <summary>
/// Port: a launcher's -matchweather, -matchtime and -matchbraking set the match's options, any of them "mission" (or
/// braking "prefs") for its choice without, in any case; a value that is none of a flag's changes nothing.
/// </summary>
TEST_CASE("network: the command line's match options set the weather, the time of day and braking")
{
    MCMatchOptions options;
    CHECK(MPReadMatchOption(MatchWeatherFlag, "LightSnow", options));
    CHECK(options.Weather == MCWeatherKind::LightSnow);
    CHECK(MPReadMatchOption(MatchTimeFlag, "dusk", options));
    CHECK(options.TimeOfDay == MCTimeOfDay::Dusk);
    CHECK(MPReadMatchOption(MatchBrakingFlag, "ON", options));
    CHECK(options.Rules == MCMatchRule::MechBraking);
    CHECK(MPReadMatchOption(MatchBrakingFlag, "off", options));
    CHECK(options.Rules == 0u);

    CHECK(!MPReadMatchOption(MatchWeatherFlag, "hail", options));
    CHECK(!MPReadMatchOption(MatchTimeFlag, "noon", options));

    // ON is the original's; OFF bans the thing, the other rules kept.
    MCMatchOptions bans{.Rules = MCMatchRule::MechBraking};
    CHECK(MPReadMatchOption(MatchArtilleryFlag, "OFF", bans));
    CHECK(MPReadMatchOption(MatchMinesFlag, "off", bans));
    CHECK(MPReadMatchOption(MatchProbesFlag, "off", bans));
    CHECK(bans.Rules ==
          (MCMatchRule::MechBraking | MCMatchRule::NoArtillery | MCMatchRule::NoMines | MCMatchRule::NoProbes));
    CHECK(MPReadMatchOption(MatchProbesFlag, "on", bans));
    CHECK(MPReadMatchOption(MatchArtilleryFlag, "on", bans));
    CHECK(MPReadMatchOption(MatchSensorsFlag, "on", bans));
    CHECK(bans.Rules == (MCMatchRule::MechBraking | MCMatchRule::NoMines));
    CHECK(!MPReadMatchOption(MatchSensorsFlag, "mission", bans));
    CHECK(MPIsMatchOptionFlag("-MATCHSENSORS"));
    CHECK(MPIsMatchOptionFlag("-matchprobes"));

    // A ban without a braking flag keeps the PREFS' braking, and the PREFS' braking keeps the bans.
    MCMatchOptions prefs;
    CHECK(MPReadMatchOption(MatchMinesFlag, "off", prefs));
    CHECK_EQ(MCWantedRules(prefs.Rules, true), MCMatchRule::MechBraking | MCMatchRule::NoMines);
    CHECK_EQ(MCWantedRules(prefs.Rules, false), MCMatchRule::NoMines);
    CHECK(MPReadMatchOption(MatchBrakingFlag, "off", prefs));
    CHECK_EQ(MCWantedRules(prefs.Rules, true), MCMatchRule::NoMines);
    CHECK(MPReadMatchOption(MatchBrakingFlag, "prefs", prefs));
    CHECK_EQ(MCWantedRules(prefs.Rules, true), MCMatchRule::MechBraking | MCMatchRule::NoMines);
    CHECK(MPReadMatchOption(MatchMinesFlag, "on", prefs));
    CHECK_EQ(MCWantedRules(prefs.Rules, true), MCMatchRule::MechBraking);
    CHECK_EQ(MCWantedRules(std::nullopt, true), MCMatchRule::MechBraking);
    CHECK_EQ(MCWantedRules(std::nullopt, false), 0u);
    CHECK(!MPIsMatchOptionFlag("-matchpilots"));
    CHECK(!MPReadMatchOption(MatchBrakingFlag, "mission", options));
    CHECK(options.Weather == MCWeatherKind::LightSnow);
    CHECK(options.TimeOfDay == MCTimeOfDay::Dusk);
    CHECK(options.Rules == 0u);

    CHECK(MPReadMatchOption(MatchWeatherFlag, "Mission", options));
    CHECK(MPReadMatchOption(MatchTimeFlag, "mission", options));
    CHECK(MPReadMatchOption(MatchBrakingFlag, "prefs", options));
    CHECK(options == MCMatchOptions{});
}

/// <summary>
/// Port: in a match, the weather and time of day its host chose are shown over the mission's own and over a player's
/// own RAIN, SNOW, DUSK or NIGHT for every mission; a player's HD weather or HD lighting off still shows the original's
/// clear day. Outside a match (none chosen) the player's and the mission's are shown as before.
/// </summary>
TEST_CASE("network: a match's weather and time of day are shown over the mission's and the player's own")
{
    const int weatherWas = GHdWeather;
    const int lightingWas = GHdLighting;
    MCWeather::SetMissionWeather(MCWeatherKind::Rain);
    MCSceneLights::SetMissionTime(MCTimeOfDay::Dusk);

    MCWeather::SetMatchWeather(MCWeatherKind::Blizzard);
    MCSceneLights::SetMatchTime(MCTimeOfDay::Night);

    for (const int setting : {HdWeatherOn, HdWeatherDownpour, HdWeatherLightSnow})
    {
        GHdWeather = setting;
        CHECK(MCWeather::Shown() == MCWeatherKind::Blizzard);
    }

    for (const int setting : {HdLightingOn, HdLightingDusk})
    {
        GHdLighting = setting;
        CHECK(MCSceneLights::TimeOfDay() == MCTimeOfDay::Night);
    }

    GHdWeather = HdWeatherOff;
    GHdLighting = HdLightingOff;
    CHECK(MCWeather::Shown() == MCWeatherKind::Clear);
    CHECK(MCSceneLights::TimeOfDay() == MCTimeOfDay::Day);

    MCWeather::SetMatchWeather(std::nullopt);
    MCSceneLights::SetMatchTime(std::nullopt);
    GHdWeather = HdWeatherOn;
    GHdLighting = HdLightingOn;
    CHECK(MCWeather::Shown() == MCWeatherKind::Rain);
    CHECK(MCSceneLights::TimeOfDay() == MCTimeOfDay::Dusk);
    GHdWeather = HdWeatherSnow;
    GHdLighting = HdLightingNight;
    CHECK(MCWeather::Shown() == MCWeatherKind::Snow);
    CHECK(MCSceneLights::TimeOfDay() == MCTimeOfDay::Night);

    GHdWeather = weatherWas;
    GHdLighting = lightingWas;
    MCWeather::SetMissionWeather(MCWeatherKind::Clear);
    MCSceneLights::SetMissionTime(MCTimeOfDay::Day);
}

namespace
{
    /// <summary>The retail game's folders (its SYSTEM.CFG's), set for the scope's lifetime.</summary>
    class RetailPaths
    {
    public:
        RetailPaths()
        {
            for (size_t i = 0; i < Paths.size(); i++)
            {
                _Saved[i] = std::exchange(Paths[i].get(), std::string(Retail[i]));
            }
        }

        ~RetailPaths()
        {
            for (size_t i = 0; i < Paths.size(); i++)
            {
                Paths[i].get() = std::move(_Saved[i]);
            }
        }

        RetailPaths(const RetailPaths&) = delete;
        RetailPaths& operator=(const RetailPaths&) = delete;

    private:
        static constexpr size_t Count = 7;
        inline static const std::array<std::reference_wrapper<std::string>, Count> Paths = {
            ObjectPath, MissionPath, WarriorPath, ProfilePath, TerrainPath, ShapesPath, SavePath};
        static constexpr std::array<std::string_view, Count> Retail = {"data\\objects\\",
                                                                       "data\\missions\\",
                                                                       "data\\missions\\warriors\\",
                                                                       "data\\missions\\profiles\\",
                                                                       "data\\terrain\\",
                                                                       "data\\sprites\\",
                                                                       "data\\savegame\\"};
        std::array<std::string, Count> _Saved;
    };

    /// <summary>The small game's mission file (DataGame).</summary>
    constexpr std::string_view DataMission = "data\\savegame\\TEST.MPK";

    /// <summary>
    /// A small game's data in memory, as the retail game lays it out: every match's gameplay files, a mission (its
    /// file, scenario, script, an include and its map) and what doesn't count (art, a map picture, another mission's
    /// script and map, a mech's pictures, a backup).
    /// </summary>
    void AddDataGame(MCMemoryFileSource& files)
    {
        files.AddFile("data\\missions\\gamesys.fit", "FITini\n[General]\nf MaxVisualRange = 600.0\nFITend\n");
        files.AddFile("data\\objects\\compbas.csv", "1,Medium Laser,3\n");
        files.AddFile("data\\objects\\object2.pak", "objects");
        files.AddFile("data\\missions\\warriors\\pbrain.abl", "module pbrain;\n#include_ \"UnitConst.abi\"\n");
        files.AddFile("data\\missions\\warriors\\UnitConst.abi", "const\n");
        files.AddFile("data\\missions\\warriors\\pbrain.bak", "an old brain");
        files.AddFile("data\\missions\\profiles\\PM100100.fit", "FITini\nFITend\n");
        files.AddFile("data\\missions\\orders.abx", "library orders;\n#include_ \"OConst.abi\"\n");
        files.AddFile("data\\missions\\OConst.abi", "const\n");
        files.AddFile("data\\missions\\purchase.fit", "FITini\n[PilotCosts]\nl Green = 1\nFITend\n");
        files.AddFile("data\\missions\\mp0703.fit", "FITini\n[TerrainSystem]\nst TerrainFileName = \"mp0703\"\n"
                                                    "[Script]\nst ScenarioScript = \"mp0703\"\nFITend\n");
        files.AddFile("data\\missions\\mp0703.abl", "module mp0703;\n#include_ \"MP7_3VAR.ABI\"\n"
                                                    "// #include_ \"gone.abi\"\n/* #include \"gone2.abi\" */\n");
        files.AddFile("data\\missions\\MP7_3VAR.ABI", "var\n");
        files.AddFile("data\\missions\\other.abl", "module other;\n");
        files.AddFile("data\\terrain\\mp0703.dat", "cells");
        files.AddFile("data\\terrain\\mp0703.elv", "heights");
        files.AddFile("data\\terrain\\mp0703.tga", "a picture");
        files.AddFile("data\\terrain\\mp0703.log.tga", "the briefing's picture");
        files.AddFile("data\\terrain\\mp0704.dat", "another map");
        files.AddFile("data\\sprites\\cm.hsp", "hot spots");
        files.AddFile("data\\sprites\\cm.inf", "FITini\nFITend\n");
        files.AddFile("data\\sprites\\legs.pak", "legs");
        files.AddFile("data\\art\\button.tga", "a button");

        // The mission file: packet 0 its settings (no PurchaseInfo block: the "purchase" list), as the game writes it.
        MCPacketFile mission;
        REQUIRE_EQ(mission.Create(DataMission), 0);
        mission.Reserve(1);
        constexpr std::string_view Settings = "FITini\n[General]\nst PurchaseFile=\"netpur\"\n"
                                              "[Multiplayer]\nst MissionName=\"mp0703\"\nFITend\n";
        mission.WritePacket(0, std::span(reinterpret_cast<const uint8_t*>(Settings.data()), Settings.size()),
                            MCPacketStorage::Raw);
        mission.Close();
    }

    /// <summary>The bytes of <paramref name="text"/>.</summary>
    std::vector<uint8_t> Bytes(std::string_view text)
    {
        return {text.begin(), text.end()};
    }

    /// <summary>
    /// The small game's (DataGame) mission's hash, in a context of its own (nothing kept from another, no archives),
    /// with <paramref name="loose"/> (path, contents) in the user folder over the install's.
    /// </summary>
    uint32_t DataGameHash(std::initializer_list<std::pair<std::string_view, std::string_view>> loose = {})
    {
        MCTestContextScope scope;
        MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
        scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
        AddDataGame(files);

        for (const auto& [path, contents] : loose)
        {
            files.AddUserFile(path, Bytes(contents));
        }

        return MCMissionDataHash(DataMission);
    }
}

/// <summary>
/// Port: a game's file report carries the hash of the mission's and every match's gameplay data as the game finds
/// them (network/MCGameDataHash.h): the mission's file, purchase list, scenario, script and what it includes, its map
/// but its pictures; the objects', pilots' and profiles' folders, the game system, the ABL libraries and what they
/// include, the mechs' shape data. A loose file of the player's own over a gameplay file changes it; one over the art,
/// a map's picture, another mission's script, a mech's pictures or a backup doesn't. Without the mission file it is 0,
/// as the original's report; it is never 0 otherwise. Every match's part is worked out once in a context and kept.
/// </summary>
TEST_CASE("network: the game data's hash changes with a gameplay file's loose override, not with an art file")
{
    const RetailPaths paths;

    {
        MCTestContextScope scope;
        MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
        scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
        AddDataGame(files);
        const std::vector<std::string> mission = MCMissionDataFiles(DataMission);
        CHECK((mission == std::vector<std::string>{"data\\missions\\mp0703.abl", "data\\missions\\mp0703.fit",
                                                   "data\\missions\\MP7_3VAR.ABI", "data\\missions\\purchase.fit",
                                                   std::string(DataMission), "data\\terrain\\mp0703.dat",
                                                   "data\\terrain\\mp0703.elv"}));
        const std::vector<std::string> shared = MCSharedDataFiles();
        CHECK((shared == std::vector<std::string>{
                             "data\\missions\\gamesys.fit", "data\\missions\\OConst.abi", "data\\missions\\orders.abx",
                             "data\\missions\\profiles\\PM100100.fit", "data\\missions\\warriors\\pbrain.abl",
                             "data\\missions\\warriors\\UnitConst.abi", "data\\objects\\compbas.csv",
                             "data\\objects\\object2.pak", "data\\sprites\\cm.hsp", "data\\sprites\\cm.inf"}));

        // Kept once worked out: a file changed later doesn't change it.
        const uint64_t kept = MCSharedDataHash();
        files.AddUserFile("data\\missions\\gamesys.fit", Bytes("FITini\nFITend\n"));
        CHECK_EQ(MCSharedDataHash(), kept);
    }

    const uint32_t original = DataGameHash();
    CHECK(original != 0u);
    CHECK_EQ(DataGameHash(), original);

    for (const std::string_view gameplay :
         {"data\\missions\\gamesys.fit", "data\\objects\\compbas.csv", "data\\missions\\warriors\\UnitConst.abi",
          "data\\missions\\OConst.abi", "data\\missions\\MP7_3VAR.ABI", "data\\missions\\mp0703.fit",
          "data\\terrain\\mp0703.elv", "data\\missions\\purchase.fit", "data\\sprites\\cm.hsp"})
    {
        MCTest::Scope scope(gameplay);
        CHECK(DataGameHash({{gameplay, "the player's own"}}) != original);
    }

    for (const std::string_view looks :
         {"data\\art\\button.tga", "data\\terrain\\mp0703.tga", "data\\terrain\\mp0703.log.tga",
          "data\\missions\\other.abl", "data\\terrain\\mp0704.dat", "data\\sprites\\legs.pak",
          "data\\missions\\warriors\\pbrain.bak"})
    {
        MCTest::Scope scope(looks);
        CHECK_EQ(DataGameHash({{looks, "the player's own"}}), original);
    }

    // A file the mission names but the game hasn't counts, as missing (here an include of its script's).
    constexpr std::string_view Script = "data\\missions\\mp0703.abl";
    constexpr std::string_view NewInclude = "data\\missions\\MP7_3NEW.ABI";
    constexpr std::string_view IncludesNew = "#include_ \"MP7_3NEW.ABI\"\n";
    CHECK(DataGameHash({{Script, IncludesNew}}) != DataGameHash({{Script, IncludesNew}, {NewInclude, "var\n"}}));

    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
    CHECK_EQ(MCMissionDataHash(DataMission), 0u);
}

/// <summary>
/// Port: the host checks a player's file report against its own hash of the mission it asked about: from a build that
/// says it hashes its data (MCMatchRule::DataHash), the same hash has the same data, another differs; from a build that
/// doesn't (an older one, or one that said nothing), any report but 0 has the file, unchecked; 0 is no file from any.
/// </summary>
TEST_CASE("network: the host checks a file report's hash only from a build that hashes its data")
{
    ClientGame client;
    MCMultiPlayer& game = *client.Game;
    game.AskedFileHash = 0x12345678;
    const auto rulesMessage = [](uint32_t rules)
    {
        std::vector<uint8_t> message = Message(MCMPMessageType::PlayerRules, {0, 0, 0, 0, 0, 0});
        PutWord(message, rules);
        return message;
    };

    game.PlayerRules[9] = MCMatchRule::Supported;
    game.PlayerRules[8] = MCMatchRule::Supported & ~MCMatchRule::DataHash;
    CHECK(game.CheckFileReport(9, 0x12345678) == MCFileCheck::Same);
    CHECK(game.CheckFileReport(9, 0x12345679) == MCFileCheck::Differs);
    CHECK(game.CheckFileReport(9, 0) == MCFileCheck::Missing);
    CHECK(game.CheckFileReport(8, 0x12345679) == MCFileCheck::Unchecked);
    CHECK(game.CheckFileReport(8, 0) == MCFileCheck::Missing);
    CHECK(game.CheckFileReport(7, 0x12345678) == MCFileCheck::Unchecked);

    // A player's rules message says it.
    game.PlayerRules.clear();
    Deliver(7, rulesMessage(MCMatchRule::DataHash));
    CHECK(game.CheckFileReport(7, 0x12345678) == MCFileCheck::Same);
}

/// <summary>
/// Port: the retail game's gameplay data are the files play reads, not its art: Arena's mission file, purchase list,
/// scenario, script and its includes, and its map's files but pictures; every match's: the game system, the objects,
/// pilots, profiles, libraries and the mechs' shape data, nothing from the art, sounds, tiles or sprites' pictures.
/// </summary>
TEST_CASE("network: the retail game's gameplay data are the files play reads, not its art")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    const RetailPaths paths;
    MCTestContextScope scope;
    const auto has = [](const std::vector<std::string>& files, std::string_view file)
    {
        return std::ranges::any_of(files, [file](const std::string& name) { return MCPort::StrICmp(name, file) == 0; });
    };

    const auto none = [](const std::vector<std::string>& files, std::string_view part)
    {
        return std::ranges::none_of(files,
                                    [part](const std::string& name)
                                    {
                                        std::string upper = name;
                                        std::ranges::transform(upper, upper.begin(), [](unsigned char c)
                                                               { return static_cast<char>(std::toupper(c)); });
                                        return upper.contains(part);
                                    });
    };

    const std::vector<std::string> mission = MCMissionDataFiles("data\\savegame\\ARENA.MPK");

    for (const std::string_view file :
         {"data\\savegame\\ARENA.MPK", "data\\missions\\purchase.fit", "data\\missions\\MP0703.FIT",
          "data\\missions\\MP0703.ABL", "data\\missions\\MP7_3VAR.ABI", "data\\missions\\MP7_3MP.ABI",
          "data\\missions\\misconst.abi", "data\\terrain\\mp0703.FIT", "data\\terrain\\mp0703.dat",
          "data\\terrain\\mp0703.ELV", "data\\terrain\\mp0703.gmm", "data\\terrain\\mp0703.OBJ",
          "data\\terrain\\mp0703.bdg", "data\\terrain\\mp0703.pre"})
    {
        MCTest::Scope fileScope(file);
        CHECK(has(mission, file));
    }

    CHECK(none(mission, ".TGA"));
    const std::vector<std::string> shared = MCSharedDataFiles();

    for (const std::string_view file :
         {"data\\missions\\gamesys.fit", "data\\objects\\compbas.csv", "data\\objects\\OBJECT2.PAK",
          "data\\objects\\allcomp.fit", "data\\missions\\orders.abx", "data\\missions\\OConst.abi",
          "data\\missions\\warriors\\pbrain.abl", "data\\missions\\warriors\\UBInit.abi",
          "data\\missions\\profiles\\PM100100.fit", "data\\sprites\\cm.hsp", "data\\sprites\\cm.inf"})
    {
        MCTest::Scope fileScope(file);
        CHECK(has(shared, file));
    }

    for (const std::string_view part :
         {"DATA\\ART\\", "DATA\\SOUND\\", "DATA\\TILES\\", "DATA\\SPRITES\\LEGS", ".SHP", ".BAK"})
    {
        MCTest::Scope partScope(part);
        CHECK(none(shared, part));
    }

    CHECK(MCMissionDataHash("data\\savegame\\ARENA.MPK") != 0u);
    CHECK(MCMissionDataHash("data\\savegame\\ARENA.MPK") != MCMissionDataHash("data\\savegame\\FORT.MPK"));
}
