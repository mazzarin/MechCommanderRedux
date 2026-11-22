#pragma once

class MCFitIniFile;

// The movers' and pilots' settings from the game system file (gamesys.fit), with MCX.EXE's values until it is read.

/// <summary>"DelayedOrderTime" (1): seconds between the group members' delayed orders.</summary>
extern float DelayedOrderTime;
/// <summary>"FireArc": firing arcs in degrees, stored halved: mechs, ground vehicles, elementals.</summary>
extern float FireArc[3];
/// <summary>"InnerSphereAntiMissile", "ClanAntiMissile": the volleys and damage per volley of an anti-missile system,
/// [0] Inner Sphere, [1] clan.</summary>
extern int32_t AntiMissileSystemStats[2][2];
/// <summary>"DamageRateFrequency" (10): seconds between damage rate checks (Mover::updateDamageTakenRate).</summary>
extern float DamageRateFrequency;
/// <summary>How far a pilot sees unaided, in meters (the scenario sets it; Mover::getVisualRange adds the probe's
/// range).</summary>
extern float MaxVisualRadius;
/// <summary>Fire range selectors 0..2 of Mover::getFireRange, in meters (short 250, medium 500, long 1000).</summary>
extern float WeaponRange[3];
/// <summary>Mover::getFireRange(-4) (75).</summary>
extern float DefaultAttackRange;
/// <summary>"NumRangeRatings" (31): range steps a weapon is rated at (calcWeaponRangeRatings).</summary>
extern int32_t NumRangeRatings;
/// <summary>"RangeRatingIncrement" (30): meters between the range steps.</summary>
extern float RangeRatingIncrement;
/// <summary>
/// Attack chance modifiers (percent), "WeaponFireModifiers": [0..2] target at short, medium, long range; [3] aimed at
/// the head, [4] torso, [5] limbs; [6] a target that isn't a mover; [7..22] copied into RankVersusChassisCombatModifier;
/// [23] a target stationary MaxStationaryTime (scaled below it). turret.cpp reads [11..14].
/// </summary>
extern float WeaponFireModifiers[30];
/// <summary>"MaxStationaryTime": seconds a target must hold still for the full stationary modifier.</summary>
extern float MaxStationaryTime;
/// <summary>"GroupOrderGoalOffset" (127).</summary>
extern float GroupOrderGoalOffset;
/// <summary>"MinRangeIncrement" (30).</summary>
extern float MinRangeIncrement;
/// <summary>"MinRangeModIncrement" (10).</summary>
extern float MinRangeModIncrement;
/// <summary>"MaxWeaponRangeMod" (45).</summary>
extern float MaxWeaponRangeMod;
/// <summary>"DisableAttackModifier" (10).</summary>
extern float DisableAttackModifier;
/// <summary>"DisableGunneryModifier" (5).</summary>
extern float DisableGunneryModifier;
/// <summary>"SalvageAttackModifier" (30).</summary>
extern float SalvageAttackModifier;
/// <summary>"PilotingCheckFactor" (1).</summary>
extern float PilotingCheckFactor;
/// <summary>"HitLevel" (10, 20). Read; no code uses it.</summary>
extern int32_t HitLevel[2];
/// <summary>"ClusterSizeSRM": read, then always 2.</summary>
extern int32_t ClusterSizeSrm;
/// <summary>"ClusterSizeLRM": read, then always 5.</summary>
extern int32_t ClusterSizeLrm;
/// <summary>"PilotCheckHalfRate" (5). Read; no code uses it.</summary>
extern float PilotCheckHalfRate;
/// <summary>"AttitudeEffect" (6x6). Read; no code uses it.</summary>
extern uint8_t AttitudeEffect[6][6];
/// <summary>Sensors "BaseSensorRollTarget" (50).</summary>
extern float SensorBaseChance;
/// <summary>Sensors "SensorSkillFactor" (10).</summary>
extern float SensorSkillFactor;
/// <summary>Sensors "BlockingObjectModifier" (-5).</summary>
extern float SensorBlockingObjectModifier;
/// <summary>Sensors "ShutdownMech" (-50).</summary>
extern float SensorShutDownMechModifier;
/// <summary>Sensors "SensorRangeModifier": four (range fraction, modifier) pairs.</summary>
extern float SensorRangeModifier[4][2];
/// <summary>Sensors "SizeModifier": three (tonnage, modifier) pairs.</summary>
extern float SensorSizeModifier[3][2];
/// <summary>Sensors "BlockingTerrainModifiers".</summary>
extern float SensorBlockingTerrain[2];
/// <summary>Skills "Sensor Contact Skill".</summary>
extern float SensorSkill;
/// <summary>"RefitRange".</summary>
extern float RefitRange;
/// <summary>Skills "Skill Attempt", per skill.</summary>
extern float SkillTry[4];
/// <summary>Refit costs, [armor, internal structure, ammo][refit vehicle, refit bay] ("RefitVehicleArmorCost",
/// "RefitBayArmorCost", ...).</summary>
extern float RefitCostArray[3][2];
/// <summary>"RefitTime".</summary>
extern float RefitTime;
/// <summary>"RefitAmount".</summary>
extern float RefitAmount;
/// <summary>"LongRangeMovementEnabled", as booleans (the FIT value 1).</summary>
extern int32_t LongRangeMovementEnabled[3];
/// <summary>Skills "Skill Success", per skill.</summary>
extern float SkillSuccess[4];
/// <summary>"AimedFireHitTable".</summary>
extern int32_t AimedFireHitTable[3];
/// <summary>"AimedFireAbort".</summary>
extern int32_t AimedFireAbort;
/// <summary>Skills "KillSkillValues".</summary>
extern float KillSkill[6];
/// <summary>Skills "WeaponHit".</summary>
extern float WeaponHit;
/// <summary>Warrior "JumpSkillMod".</summary>
extern int32_t PilotJumpMod;
/// <summary>Warrior "SkillIncreaseCap".</summary>
extern int32_t IncreaseCap;

/// <summary>"FireOddsTable" (20, 35, 50, 65, 80).</summary>
extern float FireOddsTable[5];
/// <summary>"ProfessionalismTable".</summary>
extern int8_t ProfessionalismOffsetTable[5][2];
/// <summary>"DecorumTable".</summary>
extern int8_t DecorumOffsetTable[5][2];
/// <summary>"AmmoTable": {ammo percent, attack modifier} below which the modifier applies.</summary>
extern int8_t AmmoConservationModifiers[2][2];
/// <summary>Seconds between a pilot's brain runs (2; not in the FIT).</summary>
extern float BrainUpdateFrequency;
/// <summary>"MovementUpdateFrequency" (5).</summary>
extern float MovementUpdateFrequency;
/// <summary>"CombatUpdateFrequency" (0.25).</summary>
extern float CombatUpdateFrequency;
/// <summary>"CommandUpdateFrequency" (6).</summary>
extern float CommandUpdateFrequency;
/// <summary>"ContactUpdateFrequency" (4): seconds between a sensor's scans.</summary>
extern float ContactUpdateFrequency;
/// <summary>"PilotCheckUpdateFrequency" (1).</summary>
extern float PilotCheckUpdateFrequency;
/// <summary>"PilotCheckModifiers" (25, 25).</summary>
extern int32_t PilotCheckModifierTable[2];
/// <summary>"SkillWeightings" (1, 1, 1, 1; not in the FIT): the weight of each skill in a pilot's rank.</summary>
extern float SkillWeightings[4];
/// <summary>"WarriorRankScale" (60, 75, 85, 999): the weighted skill below which each rank is.</summary>
extern float WarriorRankScale[4];
/// <summary>"GroupMoveTrailLength" ({0, 1} when missing): a group member's path is cut by
/// selectionIndex / [1] * [0] steps.</summary>
extern int32_t GroupMoveTrailLen[2];
/// <summary>"MoveTimeOut" (30 when missing).</summary>
extern float MoveTimeOut;
/// <summary>"MoveYieldTime" (1.5 when missing).</summary>
extern float MoveYieldTime;
/// <summary>"DefaultAttackRadius" (275 when missing).</summary>
extern float DefaultAttackRadius;

/// <summary>
/// Reads the movement, combat, damage, warrior, sensor and skill settings of the game system file ("Pathfinding",
/// "OptimumRange", "Mover:General", "Mover:FireWeapon", "Mover:Damage", "Components", "Warrior", "Sensors",
/// "Skills"). A missing "Mover:General" entry, warrior skill limit or "Skills" block is fatal.
/// </summary>
/// <returns>0, or the FIT error of the first entry missing (most stop the reading).</returns>
int32_t LoadMoverGameSystem(MCFitIniFile& sysFile);
