#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCMemoryFileSource.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "object/MCElemental.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCElementalType.h"
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
}

/// <summary>
/// An elemental type: "CanJump" missing means it jumps (an elemental); "Type" 0 is the player's side (1); the
/// health and the dynamics (type 3) are read.
/// </summary>
TEST_CASE("elemental type: an elemental jumps unless its file says it can't")
{
    const std::string dynamics = "[Dynamics]\nul Type = 3\n[ElementalDynamics]\nl maxElementalYawRate = 300\n"
                                 "f maxAccel = 2.0\nf maxVelocity = 4.0\n";

    {
        TypeFile file("FITini\n[Header]\nst FileType = \"ElementalType\"\n[General]\nul ID = 3\nuc Type = 0\n"
                      "st Name = \"Elemental\"\nuc MaxHealth = 11\n" +
                      dynamics + std::string(ObjectTypeBlock));
        REQUIRE_EQ(file.Result, 0);
        const auto type = std::make_shared<MCElementalType>();
        REQUIRE_EQ(type->Init(file.File, file.File.FileSize()), 0);
        CHECK(type->CanJump);
        CHECK_EQ(static_cast<int32_t>(type->Alignment), 1);
        CHECK_EQ(static_cast<int32_t>(type->MaxHealth), 11);
        CHECK(type->DynamicsType != nullptr);
    }

    {
        TypeFile file("FITini\n[Header]\nst FileType = \"ElementalType\"\n[General]\nul ID = 4\nb CanJump = FALSE\n"
                      "uc Type = 1\nst Name = \"Marine\"\nuc MaxHealth = 4\n" +
                      dynamics + std::string(ObjectTypeBlock));
        REQUIRE_EQ(file.Result, 0);
        const auto type = std::make_shared<MCElementalType>();
        REQUIRE_EQ(type->Init(file.File, file.File.FileSize()), 0);
        CHECK(!type->CanJump);
        CHECK_EQ(static_cast<int32_t>(type->Alignment), 0xff);
    }
}

/// <summary>
/// An elemental jumps one terrain vertex (32 offsets at a cost of 20); a marine doesn't jump at all. A squad has
/// one hit location and starts at 11 of 11 health.
/// </summary>
TEST_CASE("elemental: an elemental jumps one vertex, a marine not at all")
{
    const auto elemental = std::make_shared<MCElemental>();
    CHECK(elemental->ObjectClass == MCObjectClass::Elemental);
    CHECK_EQ(elemental->CurHealth, 11);
    CHECK_EQ(elemental->MaxHealth, 11);
    CHECK_EQ(elemental->InTransport(), 0);
    CHECK_EQ(elemental->CalcHitLocation(std::nullopt, -1, 0, 0), 0);

    int32_t numOffsets = 0;
    int32_t jumpCost = 0;
    CHECK_EQ(elemental->GetJumpRange(numOffsets, jumpCost), MetersPerWorldUnit * MCTerrain::MetersPerVertex);
    CHECK_EQ(numOffsets, 32);
    CHECK_EQ(jumpCost, 20);

    // A profile's jump range makes it able to jump; without one it is a marine.
    CHECK_EQ(elemental->CanJump(), 0);
    CHECK_EQ(elemental->IsMarine(), 1);
    elemental->JumpRange = 90.0f;
    CHECK_EQ(elemental->CanJump(), 1);
    CHECK_EQ(elemental->IsMarine(), 0);

    elemental->ElementalCanJump = false;
    numOffsets = 0;
    CHECK_EQ(elemental->GetJumpRange(numOffsets, jumpCost), 0.0f);
    CHECK_EQ(numOffsets, 0);
}

/// <summary>The elemental blocks of gamesys.fit: the damage on impact and the no-jump range.</summary>
TEST_CASE("elemental game system: the damage on impact and the no-jump range")
{
    const float impact = ElmDamageOnImpact;
    const float noJump = ElementalTargetNoJumpDistance;
    MCTestContextScope scope;
    scope.Context()
        .SetFiles(std::make_unique<MCMemoryFileSource>())
        .AddFile("data\\gamesys.fit", "FITini\n[Elemental:Combat]\nf NoJumpRange = 60.0\n[Elemental:Collision]\n"
                                      "f DamageOnImpact = 1.5\nFITend\n");
    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open("data\\gamesys.fit"), 0);
    CHECK_EQ(LoadElementalGameSystem(fit), 0);
    CHECK_EQ(ElmDamageOnImpact, 1.5f);
    CHECK_EQ(ElementalTargetNoJumpDistance, 60.0f);
    ElmDamageOnImpact = impact;
    ElementalTargetNoJumpDistance = noJump;
}
