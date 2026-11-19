#pragma once

#include "object/MCBuildingMarines.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCObjectType.h"

class MCDynamicsType;
class MCFile;

/// <summary>A ground vehicle type: the vehicle file's general data, internal structure, dynamics and movement.</summary>
/// <remarks>Original source: <c>object\gvehicl.cpp</c>.</remarks>
class MCGroundVehicleType : public MCObjectType
{
public:
    /// <summary>Crash avoidance from the "GroundVehicle:Movement" defaults.</summary>
    MCGroundVehicleType();
    ~MCGroundVehicleType() override;

    /// <summary>
    /// Reads the vehicle file ("GroundVehicleType"): "General" (id, alignment, name, chassis, tonnage, ammo truck,
    /// refit points, mine sweeper/layer, elemental carrier, seats, explosion), "InternalStructure" per location,
    /// "Dynamics" (type 2), "MovementSystem", then the common type data.
    /// </summary>
    /// <returns>0, -1 for the wrong file type, -0x5fffd for the wrong dynamics type, or the FIT error.</returns>
    int32_t Init(MCFile& objFile, uint32_t fileSize) override;
    /// <summary>Makes a <see cref="MCGroundVehicle"/> of this type and gives it the next object id.</summary>
    std::shared_ptr<MCBaseObject> CreateInstance() override;
    /// <summary>
    /// Port-only: <see cref="MCObjectType::MadeTypes"/>, with the type's own: the marine who bails out of the dead
    /// vehicle (MCGroundVehicle::CreateVehiclePilot).
    /// </summary>
    std::vector<int32_t> MadeTypes() const override { return MadeTypesAnd({static_cast<uint32_t>(DefaultPilotId)}); }
    /// <summary>Ramming, trees, buildings, mines and weapons against a vehicle of this type.</summary>
    int HandleCollision(MCGameObject& collidee, MCGameObject& collider) override;
    /// <summary>Kills the vehicle: disables its sensor, alarms its pilot, sets the destroyed flags and takes it off
    /// the interface.</summary>
    int HandleDestruction(MCGameObject& collidee, uint32_t colliderId) override;

    /// <summary>"ID".</summary>
    uint32_t VehicleId = 0;
    /// <summary>"Name".</summary>
    std::string Name;
    /// <summary>"Alignment", mapped 0 -> 1, 1 -> 0xff; copied to GameObject::alignment.</summary>
    uint8_t Alignment = 0;
    /// <summary>"Chassis".</summary>
    uint8_t Chassis = 0;
    /// <summary>"TonnageClass".</summary>
    float TonnageClass = 0.0f;
    /// <summary>Never read from the file (zero); copied to Mover::internalStructureTonnage.</summary>
    float InternalStructureTonnage = 0.0f;
    /// <summary>"InternalStructure": "Front", "Left", "Right", "Rear", "Turret".</summary>
    uint8_t InternalStructure[NumGroundVehicleLocations] = {};
    /// <summary>The dynamics type (a GroundVehicleDynamicsType).</summary>
    std::shared_ptr<MCDynamicsType> DynamicsType;
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
    /// <summary>"ExplosionDamage" (0 when missing).</summary>
    float ExplDmg = 0.0f;
    /// <summary>"ExplosionRadius" (0 when missing).</summary>
    float ExplRad = 0.0f;
    /// <summary>"RefitPoints"; nonzero makes the vehicle a refitter.</summary>
    int32_t RefitPoints = 0;
    /// <summary>"AmmoTruck".</summary>
    bool AmmoTruck = false;
    /// <summary>"MineSweeper".</summary>
    bool MineSweeper = false;
    /// <summary>"MinesToLay"; above 0 makes the vehicle a mine layer.</summary>
    int32_t MinesToLay = 0;
    /// <summary>"ElementalCarrier".</summary>
    bool ElementalCarrier = false;
    /// <summary>"Seats", at most <see cref="MaxGroundVehicleSeats"/>.</summary>
    uint8_t Seats = 0;
};
