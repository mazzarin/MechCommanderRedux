#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleDynamics.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCEffectSystem.h"
#include "platform/MCUtf8Path.h"

namespace
{
    /// <summary>Saves the ground vehicle settings a test changes and puts them back when it ends.</summary>
    class RestoreDefaults
    {
    public:
        RestoreDefaults()
            : _AvoidSelf(DefaultGroundVehicleCrashAvoidSelf)
            , _AvoidPath(DefaultGroundVehicleCrashAvoidPath)
            , _BlockSelf(DefaultGroundVehicleCrashBlockSelf)
            , _BlockPath(DefaultGroundVehicleCrashBlockPath)
            , _YieldTime(DefaultGroundVehicleCrashYieldTime)
            , _Collision(GvCollisionThreshold)
            , _Object(GvObjectCollisionThreshold)
            , _Tonnage(GvTonnageCollisionThreshold)
            , _Deflection(GvTreeDeflection)
            , _Sweep(GvSweepTime)
            , _Walk(GvWalkSpeed)
            , _Hill(GvHillSpeedFactor)
        {
            std::ranges::copy(GroundVehicleAttackerMoveModifier, _Attacker.begin());
            std::ranges::copy(GroundVehicleCriticalHitTable, _Critical.begin());
        }

        ~RestoreDefaults()
        {
            DefaultGroundVehicleCrashAvoidSelf = _AvoidSelf;
            DefaultGroundVehicleCrashAvoidPath = _AvoidPath;
            DefaultGroundVehicleCrashBlockSelf = _BlockSelf;
            DefaultGroundVehicleCrashBlockPath = _BlockPath;
            DefaultGroundVehicleCrashYieldTime = _YieldTime;
            GvCollisionThreshold = _Collision;
            GvObjectCollisionThreshold = _Object;
            GvTonnageCollisionThreshold = _Tonnage;
            GvTreeDeflection = _Deflection;
            GvSweepTime = _Sweep;
            GvWalkSpeed = _Walk;
            GvHillSpeedFactor = _Hill;
            std::ranges::copy(_Attacker, std::begin(GroundVehicleAttackerMoveModifier));
            std::ranges::copy(_Critical, std::begin(GroundVehicleCriticalHitTable));
        }

        RestoreDefaults(const RestoreDefaults&) = delete;
        RestoreDefaults& operator=(const RestoreDefaults&) = delete;

    private:
        int32_t _AvoidSelf;
        int32_t _AvoidPath;
        int32_t _BlockSelf;
        int32_t _BlockPath;
        float _YieldTime;
        float _Collision;
        float _Object;
        float _Tonnage;
        float _Deflection;
        float _Sweep;
        float _Walk;
        float _Hill;
        std::array<int32_t, 4> _Attacker{};
        std::array<int32_t, 11> _Critical{};
    };
}

/// <summary>
/// In MechCommander Gold no tile or overlay slows a ground vehicle: every throttle multiplier, for every chassis, is
/// 1.0 in MCX.EXE's data. A zero here pins every vehicle's throttle at 0 (the vehicles never move).
/// </summary>
TEST_CASE("gvehicl: terrain throttle multipliers leave the throttle whole")
{
    for (int32_t chassis = 0; chassis < 3; chassis++)
    {
        for (int32_t tile = 0; tile < NumThrottleTileTypes; tile++)
        {
            MCTest::Scope scope("chassis " + std::to_string(chassis) + " tile " + std::to_string(tile));
            CHECK_EQ(TileThrottleMultiplier[chassis][tile], 1.0f);
        }

        for (int32_t overlay = 0; overlay < NumThrottleOverlayTypes; overlay++)
        {
            MCTest::Scope scope("chassis " + std::to_string(chassis) + " overlay " + std::to_string(overlay));
            CHECK_EQ(OverlayThrottleMultiplier[chassis][overlay], 1.0f);
        }
    }
}

namespace
{
    /// <summary>
    /// A type file written to a scratch file outside the game install (a type loads from a child of its file, which
    /// an in-memory file can't have), open at its start; deleted when the test ends.
    /// </summary>
    struct TypeFile
    {
        explicit TypeFile(std::string_view text)
            : Path(MCFileSystem::ToUtf8(std::filesystem::temp_directory_path() / "mc_type_test.fit"))
        {
            {
                std::ofstream out(Path, std::ios::binary);
                out << text;
            }

            Result = File.Open(Path);
        }

        ~TypeFile()
        {
            File.Close();
            std::error_code ignored;
            std::filesystem::remove(Path, ignored);
        }

        TypeFile(const TypeFile&) = delete;
        TypeFile& operator=(const TypeFile&) = delete;

        std::string Path;
        MCFile File;
        int32_t Result = 0;
    };

    /// <summary>The blocks every vehicle type file ends with: dynamics (type 2) and the object type.</summary>
    constexpr std::string_view VehicleTypeTail =
        "[Dynamics]\nul Type = 2\n[VehicleDynamics]\nl maxTurretYawRate = 50\nl maxTurretYaw = 180\n"
        "l maxVehicleYawRate = 900\nf maxAccel = 1.0\nf maxVelocity = 12.0\n"
        "[ObjectType]\nl Type = 3\nl Appearance = 83886080\nl ExplosionObject = -1\nl DestroyedObject = -1\n"
        "f ExtentRadius = 10.0\nFITend\n";
}

/// <summary>
/// A refit truck's pool of refit points is kept in its turret armor slot: it hands out points while it has enough,
/// and a vehicle that isn't a refitter has none.
/// </summary>
TEST_CASE("ground vehicle: a refitter hands out the refit points its pool has, and no more")
{
    const auto truck = std::make_shared<MCGroundVehicle>();
    truck->Armor[GroundVehicleTurret].CurArmor = 50.0f;
    CHECK_EQ(truck->GetRefitPoints(), 0.0f);
    CHECK_EQ(truck->BurnRefitPoints(10.0f), 0);

    truck->Refitter = true;
    CHECK_EQ(truck->GetRefitPoints(), 50.0f);
    CHECK_EQ(truck->BurnRefitPoints(60.0f), 0);
    CHECK_EQ(truck->GetRefitPoints(), 50.0f);
    CHECK_EQ(truck->BurnRefitPoints(20.0f), 1);
    CHECK_EQ(truck->BurnRefitPoints(30.0f), 1);
    CHECK_EQ(truck->GetRefitPoints(), 0.0f);
}

/// <summary>
/// A new vehicle has its five armor locations (front, left, right, rear, turret), moves and turns its turret, holds
/// no elementals or passengers, owns no smoke, and has no mine cell yet.
/// </summary>
TEST_CASE("ground vehicle: a new vehicle has five locations, empty seats and nothing burning")
{
    const auto vehicle = std::make_shared<MCGroundVehicle>();
    CHECK(vehicle->ObjectClass == MCObjectClass::GroundVehicle);
    CHECK_EQ(vehicle->NumArmorLocations(), NumGroundVehicleLocations);
    CHECK_EQ(vehicle->NumBodyLocations(), NumGroundVehicleLocations);
    CHECK_EQ(vehicle->CanMove(), 1);
    CHECK(vehicle->TurretEnabled);
    CHECK(vehicle->Smoke == nullptr);
    CHECK_EQ(vehicle->CellRowToMine, -1);
    CHECK_EQ(vehicle->SweepTime, -1.0f);
    CHECK(std::ranges::all_of(vehicle->Elementals,
                              [](const std::weak_ptr<MCMover>& elemental) { return elemental.expired(); }));
    CHECK(std::ranges::all_of(vehicle->Passengers,
                              [](const std::weak_ptr<MCMechWarrior>& pilot) { return pilot.expired(); }));

    // A critical hit to the drive stops it.
    vehicle->MovementEnabled = false;
    CHECK_EQ(vehicle->CanMove(), 0);
}

/// <summary>
/// A vehicle type file: "Alignment" 0 is the player's side (1), 1 the enemy's (-1); the ammo truck, mine sweeper and
/// carrier flags and the seats; a missing explosion is none. Crash avoidance comes from the game system's defaults
/// unless the file's "MovementSystem" block sets it.
/// </summary>
TEST_CASE("ground vehicle type: the type file's flags, seats and alignment, crash avoidance from the defaults")
{
    RestoreDefaults defaults;
    DefaultGroundVehicleCrashAvoidSelf = 1;
    DefaultGroundVehicleCrashAvoidPath = 1;
    DefaultGroundVehicleCrashYieldTime = 2.0f;

    {
        TypeFile file(std::string("FITini\n[Header]\nst FileType = \"GroundVehicleType\"\n[General]\nul ID = 7\n"
                                  "uc Alignment = 1\nst Name = \"Harasser\"\nuc Chassis = 1\nf TonnageClass = 25.0\n"
                                  "b AmmoTruck = TRUE\nl RefitPoints = 40\nb ElementalCarrier = FALSE\nuc Seats = 2\n"
                                  "[InternalStructure]\nuc Front = 10\nuc Left = 8\nuc Right = 8\nuc Rear = 6\n"
                                  "uc Turret = 5\n[MovementSystem]\nl CrashAvoidPath = 0\nf CrashYieldTime = 4.0\n") +
                      std::string(VehicleTypeTail));
        REQUIRE_EQ(file.Result, 0);
        const auto type = std::make_shared<MCGroundVehicleType>();
        REQUIRE_EQ(type->Init(file.File, file.File.FileSize()), 0);
        CHECK_EQ(type->VehicleId, 7u);
        CHECK_EQ(static_cast<int32_t>(type->Alignment), 0xff);
        CHECK_EQ(type->Name, std::string("Harasser"));
        CHECK(type->AmmoTruck);
        CHECK_EQ(type->RefitPoints, 40);
        CHECK(!type->MineSweeper);
        CHECK(!type->ElementalCarrier);
        CHECK_EQ(static_cast<int32_t>(type->Seats), 2);
        CHECK_EQ(type->ExplDmg, 0.0f);
        CHECK_EQ(static_cast<int32_t>(type->InternalStructure[GroundVehicleTurret]), 5);
        CHECK_EQ(type->CrashAvoidSelf, 1);
        CHECK_EQ(type->CrashAvoidPath, 0);
        CHECK_EQ(type->CrashYieldTime, 4.0f);
        REQUIRE(type->DynamicsType != nullptr);
        CHECK_EQ(type->ExtentRadius, 10.0f);
    }

    {
        // Not a vehicle type file.
        TypeFile file("FITini\n[Header]\nst FileType = \"MechType\"\nFITend\n");
        REQUIRE_EQ(file.Result, 0);
        const auto type = std::make_shared<MCGroundVehicleType>();
        CHECK_EQ(type->Init(file.File, file.File.FileSize()), -1);
    }
}

/// <summary>
/// The ground vehicle blocks of gamesys.fit: the crash avoidance defaults are kept where the file has none (retail's
/// "GroundVehicle:Movement" sets only the sweeper time, the walk speed and the hill factor).
/// </summary>
TEST_CASE("ground vehicle game system: the vehicle blocks of gamesys.fit, the crash defaults kept when missing")
{
    RestoreDefaults defaults;
    DefaultGroundVehicleCrashAvoidSelf = 1;
    DefaultGroundVehicleCrashYieldTime = 2.0f;
    MCTestContextScope scope;
    scope.Context()
        .SetFiles(std::make_unique<MCMemoryFileSource>())
        .AddFile(
            "data\\gamesys.fit",
            "FITini\n[GroundVehicle:FireWeapon]\nl[4] AttackerMoveModifier = 0, 5, 10, 15\n"
            "[GroundVehicle:Damage]\nl[11] CriticalHitTable = 10, 10, 10, 10, 10, 10, 10, 10, 10, 5, 5\n"
            "[GroundVehicle:Collision]\nf collisionThreshold = 0.0\nf objectThreshold = 3.0\nf tonnageThreshold = 5.0\n"
            "f treeDeflection = 45.0\n[GroundVehicle:Movement]\nf SweeperSlowTime = 4.0\nf WalkSpeed = 9.0\n"
            "f HillSpeedFactor = 1.5\nFITend\n");
    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open("data\\gamesys.fit"), 0);
    REQUIRE_EQ(LoadGroundVehicleGameSystem(fit), 0);
    CHECK_EQ(GroundVehicleAttackerMoveModifier[3], 15);
    CHECK_EQ(GroundVehicleCriticalHitTable[10], 5);
    CHECK_EQ(GvObjectCollisionThreshold, 3.0f);
    CHECK_EQ(GvTreeDeflection, 45.0f);
    CHECK_EQ(GvSweepTime, 4.0f);
    CHECK_EQ(GvWalkSpeed, 9.0f);
    CHECK_EQ(GvHillSpeedFactor, 1.5f);
    CHECK_EQ(DefaultGroundVehicleCrashAvoidSelf, 1);
    CHECK_EQ(DefaultGroundVehicleCrashYieldTime, 2.0f);
}

/// <summary>
/// A disabled vehicle smokes; destroyed, its wreck smokes instead. Port fix (OB-148): the wreck's smoke replaces the
/// disabled one, which gives its spheres back, so a vehicle never holds more than one smoke's spheres. (The original
/// left the first smoke behind with its spheres.)
/// </summary>
TEST_CASE_ISOLATED("game: a destroyed vehicle's smoke replaces its disabled smoke, which gives its spheres back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCGroundVehicle> vehicle;

    for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId && vehicle == nullptr; partId++)
    {
        std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

        if (mover != nullptr && mover->ObjectClass == MCObjectClass::GroundVehicle && mover->IsDisabled() == 0)
        {
            vehicle = std::static_pointer_cast<MCGroundVehicle>(mover);
        }
    }

    REQUIRE(vehicle != nullptr);
    REQUIRE(MCGameContext::Current().HasEffectSystem());
    const int32_t outBefore = EffectSystem().SmokeSpheresOut();
    vehicle->Disable(0);
    REQUIRE(vehicle->Smoke != nullptr);
    const auto smokeSpheres = static_cast<int32_t>(vehicle->Smoke->Spheres.size());
    CHECK(0 < smokeSpheres);
    CHECK_EQ(EffectSystem().SmokeSpheresOut(), outBefore + smokeSpheres);

    // Destroyed, its death timer about to run out: the next update blows it up and makes the wreck's smoke.
    vehicle->Status = 2;
    vehicle->DeathTimer = 0.001f;
    vehicle->Update();
    REQUIRE(vehicle->Smoke != nullptr);
    CHECK(vehicle->DeathExplosionDone);
    CHECK_EQ(EffectSystem().SmokeSpheresOut(), outBefore + static_cast<int32_t>(vehicle->Smoke->Spheres.size()));
}
