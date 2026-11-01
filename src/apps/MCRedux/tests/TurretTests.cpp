#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCMemoryFileSource.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBattleMech.h"
#include "object/MCElemental.h"
#include "object/MCGroundVehicle.h"
#include "object/MCTurret.h"
#include "object/MCTurretType.h"
#include "terrain/MCTerrain.h"
#include "platform/MCUtf8Path.h"

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

    /// <summary>The object type block every type file ends with.</summary>
    constexpr std::string_view ObjectTypeBlock =
        "[ObjectType]\nl Type = 30\nl Appearance = 83886080\nl ExplosionObject = -1\nl DestroyedObject = -1\n"
        "f ExtentRadius = 0.0\nFITend\n";

    /// <summary>A turret type file with the required entries and <paramref name="extra"/>.</summary>
    std::string TurretFile(std::string_view extra)
    {
        return std::string("FITini\n[TurretData]\nul DmgLevel = 40\nf AttackRadius = 300.0\nf MaxTurretYawRate = 45.0\n"
                           "l WeaponType = 12\nl PilotSkill = 10\n") +
               std::string(extra) + std::string(ObjectTypeBlock);
    }
}

/// <summary>
/// A turret type's optional entries take their defaults when missing: 20 tons, a little extent of 20, the turret
/// name string 0xa4, no offsets, and a closed damage level equal to the open one.
/// </summary>
TEST_CASE("turret type: missing optional entries take their defaults")
{
    TypeFile file(TurretFile(""));
    REQUIRE_EQ(file.Result, 0);
    const auto type = std::make_shared<MCTurretType>();
    REQUIRE_EQ(type->Init(file.File, file.File.FileSize()), 0);
    CHECK_EQ(type->DmgLevel, 40u);
    CHECK_EQ(type->DmgLevelClosed, 40u);
    CHECK_EQ(type->Tonnage, 20.0f);
    CHECK_EQ(type->LittleExtent, 20.0f);
    CHECK_EQ(type->BuildingName, 0xa4);
    CHECK_EQ(type->FireOffsetX, 0);
    CHECK_EQ(type->AttackRadius, 300.0f);
    CHECK_EQ(type->WeaponType, 12);
    CHECK_EQ(type->PilotSkill, 10);
    CHECK_EQ(type->TypeClass, 30);
}

/// <summary>
/// Original behaviour (OB-011): the blown, normal and damage effect ids are kept only when the file has a
/// "DmgLevelClosed"; without one all three are none (-1), whatever the file says. With one, a missing effect id
/// reads as 0.
/// </summary>
TEST_CASE("turret type: the effect ids need DmgLevelClosed (OB-011)")
{
    {
        TypeFile file(TurretFile("ul BlownEffectId = 300\nul DamageEffectId = 73\n"));
        REQUIRE_EQ(file.Result, 0);
        const auto type = std::make_shared<MCTurretType>();
        REQUIRE_EQ(type->Init(file.File, file.File.FileSize()), 0);
        CHECK_EQ(type->BlownEffectId, 0xffffffffu);
        CHECK_EQ(type->NormalEffectId, 0xffffffffu);
        CHECK_EQ(type->DamageEffectId, 0xffffffffu);
    }

    {
        TypeFile file(TurretFile("ul DmgLevelClosed = 80\nul BlownEffectId = 300\nul DamageEffectId = 73\n"));
        REQUIRE_EQ(file.Result, 0);
        const auto type = std::make_shared<MCTurretType>();
        REQUIRE_EQ(type->Init(file.File, file.File.FileSize()), 0);
        CHECK_EQ(type->DmgLevelClosed, 80u);
        CHECK_EQ(type->BlownEffectId, 300u);
        CHECK_EQ(type->NormalEffectId, 0u);
        CHECK_EQ(type->DamageEffectId, 73u);
    }
}

/// <summary>A required turret entry missing is the error the load stops with.</summary>
TEST_CASE("turret type: a missing required entry stops the load")
{
    TypeFile file(std::string("FITini\n[TurretData]\nul DmgLevel = 40\nf AttackRadius = 300.0\n") +
                  std::string(ObjectTypeBlock));
    REQUIRE_EQ(file.Result, 0);
    const auto type = std::make_shared<MCTurretType>();
    CHECK_EQ(type->Init(file.File, file.File.FileSize()), VARIABLE_NOT_FOUND);
}

/// <summary>
/// A new turret is armed (OB-014: the port's turrets always are), open, awake, off the network roster, with no
/// target and nothing burning; its weapon is ready once the recycle time has passed, and only while it is open.
/// </summary>
TEST_CASE("turret: a new turret is armed and open, and ready once its weapon has recycled")
{
    const float scenarioTime = ScenarioTime;
    const auto turret = std::make_shared<MCTurret>();
    CHECK(turret->WeaponEnabled);
    CHECK(turret->WeaponDeployed);
    CHECK(turret->Awake);
    CHECK(turret->JustCreated);
    CHECK_EQ(turret->NetRosterIndex, -1);
    CHECK(LockObject(turret->Target) == nullptr);
    CHECK(turret->FireObject == nullptr);
    CHECK(turret->Smoke == nullptr);

    ScenarioTime = 10.0f;
    turret->ReadyTime = 12.0f;
    CHECK_EQ(turret->IsWeaponReady(), 0);
    ScenarioTime = 12.0f;
    CHECK_EQ(turret->IsWeaponReady(), 1);
    turret->WeaponDeployed = false;
    CHECK_EQ(turret->IsWeaponReady(), 0);
    ScenarioTime = scenarioTime;
}

/// <summary>
/// A turret's weapon fire chunks queue without a limit (the original's lists held 8 but were checked against 128):
/// a network update copies out as many as its buffer holds, and clearing a list says how many it held.
/// </summary>
TEST_CASE("turret: the weapon fire chunks queue past eight and are copied out as far as the buffer holds")
{
    const auto turret = std::make_shared<MCTurret>();

    for (uint32_t i = 0; i < 10; i++)
    {
        turret->WeaponFireChunks[0].push_back(0x100 + i);
    }

    std::array<uint32_t, 16> buffer{};
    CHECK_EQ(turret->GrabWeaponFireChunks(0, buffer), 10);
    CHECK_EQ(buffer[9], 0x109u);
    CHECK_EQ(buffer[10], 0u);
    std::array<uint32_t, 4> small{};
    CHECK_EQ(turret->GrabWeaponFireChunks(0, small), 4);
    CHECK_EQ(small[3], 0x103u);
    CHECK_EQ(turret->ClearWeaponFireChunks(0), 10);
    CHECK(turret->WeaponFireChunks[0].empty());
    CHECK_EQ(turret->ClearWeaponFireChunks(1), 0);
}

/// <summary>
/// A turret picks its target from what enters its range: an enemy (the other side) mech, vehicle or elemental that
/// is neither disabled nor destroyed, when it is nearer than the current target. Its own side, and wrecks, never.
/// </summary>
TEST_CASE("turret: the nearest whole enemy that comes into range becomes its target")
{
    const auto type = std::make_shared<MCTurretType>();
    const auto turret = std::make_shared<MCTurret>();
    turret->Alignment = -1;
    turret->Position = MCVector3D(0.0f, 0.0f, 0.0f);

    const auto mover = [](const auto& object, float x, int32_t alignment)
    {
        object->Alignment = alignment;
        object->Position = MCVector3D(x, 0.0f, 0.0f);
    };

    const auto friendly = std::make_shared<MCBattleMech>();
    mover(friendly, 10.0f, -1);
    CHECK_EQ(type->HandleCollision(*turret, *friendly), 1);
    CHECK(LockObject(turret->Target) == nullptr);

    const auto far = std::make_shared<MCBattleMech>();
    mover(far, 100.0f, 1);
    type->HandleCollision(*turret, *far);
    CHECK(LockObject(turret->Target) == far);

    const auto farther = std::make_shared<MCBattleMech>();
    mover(farther, 200.0f, 1);
    type->HandleCollision(*turret, *farther);
    CHECK(LockObject(turret->Target) == far);

    const auto disabled = std::make_shared<MCGroundVehicle>();
    mover(disabled, 50.0f, 1);
    disabled->Status = 1;
    type->HandleCollision(*turret, *disabled);
    CHECK(LockObject(turret->Target) == far);

    const auto near = std::make_shared<MCElemental>();
    mover(near, 30.0f, 1);
    type->HandleCollision(*turret, *near);
    CHECK(LockObject(turret->Target) == near);

    // Changing sides drops the target.
    turret->SetAlignment(1);
    CHECK(LockObject(turret->Target) == nullptr);
}
