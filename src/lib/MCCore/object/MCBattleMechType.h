#pragma once

#include "object/MCMechGameSystem.h"
#include "object/MCObjectType.h"

class MCDynamicsType;
class MCFile;
class MCFitIniFile;

/// <summary>The object type of the sparks a hit item throws (MCBattleMech::HitInventoryItem).</summary>
inline constexpr uint32_t MechHitSparksType = 0x3f;
/// <summary>The object type of each jump jet's flame as a mech jumps (MCBattleMech::CreateJumpFX).</summary>
inline constexpr uint32_t MechJumpJetType = 0x1c6;

/// <summary>A mech type: the mech file's header, internal structure, debris, dynamics and movement settings, and the
/// appearance's hot spots.</summary>
/// <remarks>Original source: <c>object\mech.cpp</c>.</remarks>
class MCBattleMechType : public MCObjectType
{
public:
    /// <summary>Debris pieces -1; crash avoidance from the "Mech:Movement" defaults.</summary>
    MCBattleMechType();
    ~MCBattleMechType() override;

    /// <summary>
    /// Reads the mech file: "General" (id, type, name, chassis, tonnage, explosion, endo steel, internal structure
    /// tonnage), "InternalStructure", "Debris", "Dynamics", "MovementSystem", hot spots, then the common type data.
    /// </summary>
    /// <returns>0, -1 for the wrong file type, -0x5fffd for the wrong dynamics type, or the error of the part that
    /// failed.</returns>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>Makes a <see cref="MCBattleMech"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>
    /// Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own: a hit item's sparks, the jump jets' flames
    /// and the arms thrown off (none below 1: 0xffffffff is -1).
    /// </summary>
    std::vector<int32_t> MadeTypes() const override
    {
        return MadeTypesAnd({MechHitSparksType, MechJumpJetType, RightArmDebrisId, LeftArmDebrisId});
    }

    /// <summary>Ramming, jumping onto, trees, buildings and train cars against a mech of this type.</summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    /// <summary>Kills the mech: disables its sensor, alarms its pilot, destroys every location and sets the
    /// friendly/enemy destroyed flag (a withdrawing mech only leaves the interface).</summary>
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override;
    /// <summary>
    /// Reads the appearance's hot spots: the mech file's "HotSpots" block, then the .hsp (hot spot packets), .out
    /// (outlines), .inf (packet counts) and .jmp (jump jet offsets) files.
    /// </summary>
    /// <returns>0, or the first error.</returns>
    int32_t LoadHotSpots(MCFitIniFile& mechFile);
    /// <summary>
    /// Lays out <see cref="HotSpotPackets"/> from the gestures' raw packets as the original read them: a packet
    /// shorter than its declared hot spots reads on into the blocks the type data heap put before it.
    /// </summary>
    void LayOutHotSpotPackets(const std::vector<std::vector<uint8_t>>& packets);
    /// <summary>The hot spot offsets of <paramref name="gesture"/>: frames × hot spots × (x, y, z).</summary>
    std::span<const float> GestureHotSpots(uint32_t gesture) const
    {
        return std::span<const float>(HotSpotPackets[gesture]).subspan(3);
    }

    /// <summary>The outline packet of <paramref name="gesture"/> as floats (the jump and fall heights), empty where
    /// the gesture has none.</summary>
    std::span<const float> GestureOutline(uint32_t gesture) const;

    /// <summary>The offset of jump jet <paramref name="jet"/> at frame <paramref name="frame"/> of the jump, from
    /// <see cref="JumpData"/>; frames past the file's hold its last one.</summary>
    MCVector3D JumpJetOffset(int32_t jet, int32_t frame) const;

    /// <summary>"ID".</summary>
    uint32_t MechId = 0;
    /// <summary>"Name".</summary>
    std::string Name;
    /// <summary>"Type", mapped.</summary>
    uint8_t MechType = 0;
    /// <summary>"Chassis".</summary>
    uint8_t Chassis = 0;
    /// <summary>"TonnageClass".</summary>
    float TonnageClass = 0.0f;
    /// <summary>"EndoSteel".</summary>
    uint32_t EndoSteel = 0;
    /// <summary>"InternalStructureTonnage".</summary>
    float InternalStructureTonnage = 0.0f;
    /// <summary>"InternalStructure" per body location.</summary>
    uint8_t InternalStructure[NumMechBodyLocations] = {};
    /// <summary>The dynamics type ("Dynamics" block, type 1).</summary>
    std::shared_ptr<MCDynamicsType> DynamicsType;
    /// <summary>The hot spot file's last packet: 32 bytes per gesture.</summary>
    std::vector<uint8_t> HotSpotData;
    /// <summary>"numHotSpotPackets" of the .inf file: one per gesture.</summary>
    uint32_t NumHotSpotPackets = 0;
    /// <summary>"numWeapons".</summary>
    uint32_t NumWeapons = 0;
    /// <summary>"numOthers".</summary>
    uint32_t NumOthers = 0;
    /// <summary>"numFramesPerHotSpot" per gesture (one slot more than the gestures, zero).</summary>
    std::vector<uint32_t> NumFramesPerHotSpot;
    /// <summary>The mech file's "weapon%d" hot spot entries: where each weapon is mounted (1 torso, 2 left arm, 3
    /// right arm).</summary>
    std::vector<uint32_t> WeaponHotSpots;
    /// <summary>The .jmp file: the jump jets' offsets per frame.</summary>
    std::vector<float> JumpData;
    /// <summary>The jump jets a .jmp file holds, one after the other: all its frames of one, then the other's.</summary>
    static constexpr size_t JumpJetCount = 2;
    /// <summary>The .out file's packet per gesture (empty where it has none; one slot more than the gestures).</summary>
    std::vector<std::vector<uint8_t>> GestureOutlines;
    /// <summary>"FootprintType".</summary>
    int32_t FootprintType = 1;
    /// <summary>"RightArmPiece": the debris type of the right arm, -1 for none.</summary>
    uint32_t RightArmDebrisId = 0xffffffff;
    /// <summary>"LeftArmPiece".</summary>
    uint32_t LeftArmDebrisId = 0xffffffff;
    /// <summary>"DestroyedPiece".</summary>
    uint32_t DestroyedPiece = 0xffffffff;
    /// <summary>"CrashAvoidSelf".</summary>
    int32_t CrashAvoidSelf = 0;
    /// <summary>"CrashAvoidPath".</summary>
    int32_t CrashAvoidPath = 0;
    /// <summary>"CrashBlockSelf".</summary>
    int32_t CrashBlockSelf = 0;
    /// <summary>"CrashBlockPath".</summary>
    int32_t CrashBlockPath = 0;
    /// <summary>"CrashYieldTime".</summary>
    float CrashYieldTime = 0.0f;
    /// <summary>"ExplosionDamage".</summary>
    float ExplDmg = 0.0f;
    /// <summary>"ExplosionRadius".</summary>
    float ExplRad = 0.0f;
    /// <summary>
    /// Each gesture's hot spot packet as the original read it: three floats before it, then its offsets, then what
    /// lay past it, up to the declared hot spots (see <see cref="LayOutHotSpotPackets"/>). The Commando's (cm.hsp)
    /// gestures 0-14 hold 3 hot spots, not numWeapons + numOthers = 6.
    /// </summary>
    std::vector<std::vector<float>> HotSpotPackets;
    /// <summary>The floats each gesture's packet holds in the file.</summary>
    std::vector<uint32_t> HotSpotPacketShippedFloats;
};
