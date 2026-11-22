#include "stdafx.h"
#include "object/MCMoverGameSystem.h"
#include "ai/MCMoveGeometry.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "object/MCGameSystemReader.h"
#include "mission/MCMission.h"
#include "object/MCGameObject.h"
#include "object/MCSensorSystem.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"

float DelayedOrderTime = 1.0f;
float FireArc[3] = {};
int32_t AntiMissileSystemStats[2][2] = {{1, 2}, {2, 2}};
float DamageRateFrequency = 10.0f;
float MaxVisualRadius = 0.0f;
float WeaponRange[3] = {250.0f, 500.0f, 1000.0f};
float DefaultAttackRange = 75.0f;
int32_t NumRangeRatings = 31;
float RangeRatingIncrement = 30.0f;
float MaxStationaryTime = 0.0f;
float GroupOrderGoalOffset = 127.0f;
float MinRangeIncrement = 30.0f;
float MinRangeModIncrement = 10.0f;
float MaxWeaponRangeMod = 45.0f;
float DisableAttackModifier = 10.0f;
float DisableGunneryModifier = 5.0f;
float SalvageAttackModifier = 30.0f;
float PilotingCheckFactor = 1.0f;
int32_t HitLevel[2] = {10, 20};
int32_t ClusterSizeSrm = 2;
int32_t ClusterSizeLrm = 5;
float PilotCheckHalfRate = 5.0f;
uint8_t AttitudeEffect[6][6] = {{0x32, 0x4b, 0x05, 0xfe, 0x00, 0x03}, {0x28, 0x3c, 0x0a, 0xff, 0x01, 0x05},
                                {0x1e, 0x32, 0x0f, 0x00, 0x02, 0x0a}, {0x14, 0x28, 0x14, 0x01, 0x03, 0x0f},
                                {0x0a, 0x19, 0x19, 0x01, 0x04, 0x14}, {0x00, 0x00, 0x20, 0x02, 0x05, 0x80}};
float SensorBaseChance = 50.0f;
float SensorSkillFactor = 10.0f;
float SensorBlockingObjectModifier = -5.0f;
float SensorShutDownMechModifier = -50.0f;
float SensorRangeModifier[4][2] = {{0.25f, 50.0f}, {0.15f, 20.0f}, {0.35f, 0.0f}, {0.25f, -25.0f}};
float SensorSizeModifier[3][2] = {{25.0f, -10.0f}, {60.0f, 0.0f}, {999.0f, 15.0f}};
float SensorBlockingTerrain[2] = {-25.0f, -5.0f};
float SensorSkill = 0.0f;
float RefitRange = 0.0f;
float SkillTry[4] = {};
float RefitCostArray[3][2] = {};
float RefitTime = 0.0f;
float RefitAmount = 0.0f;
int32_t LongRangeMovementEnabled[3] = {};
float SkillSuccess[4] = {};
int32_t AimedFireHitTable[3] = {};
int32_t AimedFireAbort = 0;
float KillSkill[6] = {};
float WeaponHit = 0.0f;
int32_t PilotJumpMod = 0;
int32_t IncreaseCap = 0;

float FireOddsTable[5] = {20.0f, 35.0f, 50.0f, 65.0f, 80.0f};
int8_t ProfessionalismOffsetTable[5][2] = {{10, 10}, {20, 5}, {30, 0}, {40, 5}, {100, -10}};
int8_t DecorumOffsetTable[5][2] = {{10, 10}, {20, 5}, {30, 0}, {40, 5}, {100, -10}};
int8_t AmmoConservationModifiers[2][2] = {{50, -5}, {20, -10}};
float BrainUpdateFrequency = 2.0f;
float MovementUpdateFrequency = 5.0f;
float CombatUpdateFrequency = 0.25f;
float CommandUpdateFrequency = 6.0f;
float ContactUpdateFrequency = 4.0f;
float PilotCheckUpdateFrequency = 1.0f;
int32_t PilotCheckModifierTable[2] = {25, 25};
float SkillWeightings[4] = {1.0f, 1.0f, 1.0f, 1.0f};
float WarriorRankScale[4] = {60.0f, 75.0f, 85.0f, 999.0f};
int32_t GroupMoveTrailLen[2] = {0, 1};
float MoveTimeOut = 30.0f;
float MoveYieldTime = 1.5f;
float DefaultAttackRadius = 275.0f;

namespace
{
    /// <summary>Copies a table read as chars into the signed table it fills, row by row.</summary>
    template <size_t Rows, size_t Columns>
    void CopyTable(const std::array<char, Rows * Columns>& source, int8_t (&table)[Rows][Columns])
    {
        for (size_t i = 0; i < Rows * Columns; i++)
        {
            table[i / Columns][i % Columns] = static_cast<int8_t>(source[i]);
        }
    }

    /// <summary>A table of pairs, flattened.</summary>
    template <size_t Rows> std::array<float, Rows * 2> Flatten(const float (&table)[Rows][2])
    {
        std::array<float, Rows * 2> flat{};

        for (size_t i = 0; i < Rows * 2; i++)
        {
            flat[i] = table[i / 2][i % 2];
        }

        return flat;
    }

    /// <summary>Copies a flat float table into the pairs it fills.</summary>
    template <size_t Rows> void CopyPairs(const std::array<float, Rows * 2>& source, float (&table)[Rows][2])
    {
        for (size_t i = 0; i < Rows * 2; i++)
        {
            table[i / 2][i % 2] = source[i];
        }
    }

    /// <summary>Reads a float of the "Mover:General" block; a missing one is reported, and reads as zero.</summary>
    void ReadRequired(MCFitIniFile& sysFile, std::string_view name, float& value)
    {
        Assert(MCGameSystemReader::ReadValue(sysFile, name, value) == 0, 0,
               std::format("Couldn't find {} in Mover:General block in gamesys.fit", name));
    }

    /// <summary>Reads a warrior skill limit; a missing one is reported, and reads as zero.</summary>
    template <MCFitValue T>
    void ReadSkillLimit(MCFitIniFile& sysFile, std::string_view name, std::string_view what, T& value)
    {
        const int32_t result = MCGameSystemReader::ReadValue(sysFile, name, value);
        Assert(result == 0, static_cast<uint32_t>(result),
               std::format(" Couldn't find {} variable in Warrior block of gamesys.fit ", what));
    }
}

auto LoadMoverGameSystem(MCFitIniFile& sysFile) -> int32_t
{
    // Each check below is where the original returned the first error; what follows it ran only without one.
    MCGameSystemReader read(sysFile);
    read.Block("Pathfinding");
    std::array<int32_t, 3> longRangeEnabled{};
    read.Array("LongRangeMovementEnabled", longRangeEnabled);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    for (size_t i = 0; i < longRangeEnabled.size(); i++)
    {
        LongRangeMovementEnabled[i] = longRangeEnabled[i] == 1 ? 1 : 0;
    }

    read.Value("SimplePathTileRange", SimpleMovePathRange);
    read.Value("DelayedOrderTime", DelayedOrderTime);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    MoveTimeOut = sysFile.Read<float>("MoveTimeOut").value_or(30.0f);
    MoveYieldTime = sysFile.Read<float>("MoveYieldTime").value_or(1.5f);

    // The original reads it twice, to the same result.
    if (!sysFile.ReadArray<int32_t>("GroupMoveTrailLength", GroupMoveTrailLen).has_value())
    {
        GroupMoveTrailLen[0] = 0;
        GroupMoveTrailLen[1] = 1;
    }

    read.Value("GroupOrderGoalOffset", GroupOrderGoalOffset);
    read.Array("MoveMarginOfError", MoveMarginOfError);
    read.Array("OverlayCellCosts", OverlayWeightTable);
    read.Block("OptimumRange");
    read.Value("NumRangeRatings", NumRangeRatings);
    read.Value("RangeRatingIncrement", RangeRatingIncrement);
    read.Value("MinRangeIncrement", MinRangeIncrement);
    read.Value("MinRangeModIncrement", MinRangeModIncrement);
    read.Value("MaxWeaponRangeMod", MaxWeaponRangeMod);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    Assert(sysFile.SeekBlock("Mover:General") == 0, 0, "Couldn't find Mover:General block in gamesys.fit");
    ReadRequired(sysFile, "BlockCaptureRange", BlockCaptureRange);
    ReadRequired(sysFile, "RefitTime", RefitTime);
    ReadRequired(sysFile, "RefitRange", RefitRange);
    ReadRequired(sysFile, "RefitAmount", RefitAmount);
    ReadRequired(sysFile, "RefitVehicleArmorCost", RefitCostArray[0][0]);
    ReadRequired(sysFile, "RefitVehicleInternalCost", RefitCostArray[1][0]);
    ReadRequired(sysFile, "RefitVehiclePointsToAmmo", RefitCostArray[2][0]);
    ReadRequired(sysFile, "RefitBayArmorCost", RefitCostArray[0][1]);
    ReadRequired(sysFile, "RefitBayInternalCost", RefitCostArray[1][1]);
    ReadRequired(sysFile, "RefitBayAmmoCost", RefitCostArray[2][1]);

    read.Block("Mover:FireWeapon");

    if (read.Error() != 0)
    {
        return read.Error();
    }

    // Missing modifiers keep the defaults; [7..22] also fill RankVersusChassisCombatModifier's columns 1..4.
    if (sysFile.ReadArray<float>("WeaponFireModifiers", WeaponFireModifiers).has_value())
    {
        for (int32_t rank = 0; rank < 4; rank++)
        {
            for (int32_t chassis = 0; chassis < 4; chassis++)
            {
                RankVersusChassisCombatModifier[rank][chassis + 1] = WeaponFireModifiers[7 + rank * 4 + chassis];
            }
        }
    }

    read.Array("FireArc", FireArc);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    // Stored as half arcs.
    for (float& arc : FireArc)
    {
        arc = static_cast<float>(arc * 0.5);
    }

    read.Value("AimedFireAbort", AimedFireAbort);
    read.Array("AimedFireHitTable", AimedFireHitTable);
    read.Value("DisableAttackModifier", DisableAttackModifier);
    read.Value("DisableGunneryModifier", DisableGunneryModifier);
    read.Value("SalvageAttackModifier", SalvageAttackModifier);
    read.Value("MaxStationaryTime", MaxStationaryTime);
    read.Block("Mover:Damage");
    read.Array("HitLevel", HitLevel);
    read.Value("PilotingCheckFactor", PilotingCheckFactor);

    // The cluster sizes are read, then fixed at 2 and 5.
    read.Block("Components");
    read.Value("ClusterSizeSRM", ClusterSizeSrm);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    ClusterSizeSrm = 2;
    read.Value("ClusterSizeLRM", ClusterSizeLrm);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    ClusterSizeLrm = 5;
    read.Array("InnerSphereAntiMissile", AntiMissileSystemStats[0]);
    read.Array("ClanAntiMissile", AntiMissileSystemStats[1]);
    read.Block("Warrior");

    if (read.Error() != 0)
    {
        return read.Error();
    }

    DefaultAttackRadius = sysFile.Read<float>("DefaultAttackRadius").value_or(275.0f);
    read.Array("WarriorRankScale", WarriorRankScale);
    std::array<char, 10> professionalism{};
    read.Array("ProfessionalismTable", professionalism);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    CopyTable<5, 2>(professionalism, ProfessionalismOffsetTable);
    std::array<char, 10> decorum{};
    read.Array("DecorumTable", decorum);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    CopyTable<5, 2>(decorum, DecorumOffsetTable);
    std::array<char, 4> ammo{};
    read.Array("AmmoTable", ammo);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    CopyTable<2, 2>(ammo, AmmoConservationModifiers);
    read.Value("PilotCheckHalfRate", PilotCheckHalfRate);
    read.Array("PilotCheckModifiers", PilotCheckModifierTable);
    read.Value("DamageRateFrequency", DamageRateFrequency);
    std::array<char, 36> attitudeEffect{};
    read.Array("AttitudeEffect", attitudeEffect);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    for (size_t i = 0; i < attitudeEffect.size(); i++)
    {
        AttitudeEffect[i / 6][i % 6] = static_cast<uint8_t>(attitudeEffect[i]);
    }

    read.Value("MovementUpdateFrequency", MovementUpdateFrequency);
    read.Value("CombatUpdateFrequency", CombatUpdateFrequency);
    read.Value("CommandUpdateFrequency", CommandUpdateFrequency);
    read.Value("ContactUpdateFrequency", ContactUpdateFrequency);
    read.Value("PilotCheckUpdateFrequency", PilotCheckUpdateFrequency);
    read.Array("FireOddsTable", FireOddsTable);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    ReadSkillLimit(sysFile, "SkillIncreaseCap", "SkillCap", IncreaseCap);
    ReadSkillLimit(sysFile, "SkillMax", "SkillMax", MaxPilotSkill);
    ReadSkillLimit(sysFile, "SkillMin", "SkillMin", MinPilotSkill);
    ReadSkillLimit(sysFile, "JumpSkillMod", "JumpSkillMod", PilotJumpMod);

    read.Block("Sensors");
    char automaticSuccess = 0;
    read.Value("AutomaticSuccess", automaticSuccess);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    SensorAutomaticSuccess = automaticSuccess == 1 ? 1 : 0;
    read.Array("SensorSkillMoveRange", SensorSkillMoveRange);

    // Read in place: the elements before an error are kept.
    std::array<float, 8> moveFactors = Flatten(SensorSkillMoveFactor);
    read.Array("SensorSkillMoveFactor", moveFactors);
    CopyPairs<4>(moveFactors, SensorSkillMoveFactor);
    read.Array("SensorModifiers", SensorModifier);
    read.Value("BaseSensorRollTarget", SensorBaseChance);
    read.Value("SensorSkillFactor", SensorSkillFactor);
    read.Value("BlockingObjectModifier", SensorBlockingObjectModifier);
    read.Value("ShutdownMech", SensorShutDownMechModifier);
    std::array<float, 8> rangeModifiers{};
    read.Array("SensorRangeModifier", rangeModifiers);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    CopyPairs<4>(rangeModifiers, SensorRangeModifier);
    std::array<float, 6> sizeModifiers{};
    read.Array("SizeModifier", sizeModifiers);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    CopyPairs<3>(sizeModifiers, SensorSizeModifier);
    // Read and dropped.
    std::array<int32_t, 9> sensorMasterIds{};
    read.Array("SensorMasterIDs", sensorMasterIds);
    read.Array("BlockingTerrainModifiers", SensorBlockingTerrain);

    if (read.Error() != 0)
    {
        return read.Error();
    }

    if (const int32_t result = sysFile.SeekBlock("Skills"); result != 0)
    {
        Fatal(result, "Couldn't find skill block in gamesys.fit");
    }

    read.Array("Skill Attempt", SkillTry);
    read.Array("Skill Success", SkillSuccess);
    read.Value("WeaponHit", WeaponHit);
    read.Array("KillSkillValues", KillSkill);
    read.Value("Sensor Contact Skill", SensorSkill);
    return read.Error();
}
