#pragma once

#include "object/MCGameObject.h"

/// <summary>
/// Port-only: the sensor blip's poses a second. OB-188 (fixed): the original moved mechs', vehicles', buildings' and
/// fires' blips on by a clock that dropped what was left of a pose, and turrets', train cars' and elementals' a pose a
/// frame drawn, so a blip spun by the frame rate (at 15 frames a second the first kind skipped every other pose; at 60
/// it played 12 poses a second, the second kind 60) and pinged each time round.
/// </summary>
inline constexpr float SensorBlipPosesPerSecond = 15.0f;

/// <summary>
/// Port-only (OB-188): the whole poses a sensor blip moves on in <paramref name="seconds"/> more, at
/// <see cref="SensorBlipPosesPerSecond"/>; <paramref name="carried"/> keeps the part of a pose left over.
/// </summary>
int32_t MCSensorBlipPoses(float& carried, float seconds);

/// <summary>
/// A game object with the full state the interface uses: tonnage, team, damage, sensor contact, screen position,
/// awake/exists/use-me/captured flags, explosion, salvage, combat value and attackers. Buildings, turrets, gates and
/// movers derive from it.
/// </summary>
/// <remarks>Original source: <c>object\gameobj.h</c>, <c>object\gameobj.cpp</c>.</remarks>
class MCBigGameObject : public MCGameObject
{
public:
    MCBigGameObject() { ObjectClass = MCObjectClass::BigGameObject; }
    /// <summary>Removes the object from the contact manager and the object map.</summary>
    ~MCBigGameObject() override;

    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    /// <summary>Makes the type's explosion and its destroyed object at the object's position.</summary>
    /// <returns>0xBEADDEAD.</returns>
    int32_t Kill() override;
    int32_t Update() override { return 0; }
    void Render() override {}
    int GetUseMe() override { return (Flags & 4) >> 2; }
    void SetAwake(int awake) override
    {
        Flags &= ~1;

        if (awake)
        {
            Flags |= 1;
        }
    }

    int32_t LoadProfile(MCFitIniFile& profileFile) override { return 0; }
    /// <summary>Projects the object through the main camera; on screen, remembers the turn.</summary>
    int OnScreen() override;
    /// <summary>
    /// Draws the object as a sensor contact: <paramref name="shape"/>'s poses (the blip sized by tonnage) at its screen
    /// position, one after another (<see cref="MCSensorBlipPoses"/>; every pose shown at 15 frames a second or more),
    /// pinging each time it goes round. Nothing without a shape.
    /// </summary>
    void RenderSensorBlip(std::span<uint8_t> shape);
    std::shared_ptr<MCTeam> GetTeam() override { return Team.lock(); }
    int IsPotentialContact() override { return PotentialContact.lock() != nullptr; }
    /// <summary>
    /// Adds the object to the potential contact manager with <paramref name="contactType"/> (or moves it there),
    /// or removes it for 0.
    /// </summary>
    void SetPotentialContact(int32_t contactType) override;
    std::shared_ptr<MCPotentialContact> GetPotentialContact() override { return PotentialContact.lock(); }
    void UpdateContactStatus(MCTeam& contactTeam) override;
    void SetContactTagged(int32_t teamId, int tagged) override;
    int GetContactTagged(int32_t teamId) override;
    int32_t GetContactType(int32_t teamId) override;
    int32_t GetContactType(int32_t teamId, int& tagged) override;
    MCVector2D GetScreenPos(int32_t whichOne) override;
    float GetDamage() override { return Damage; }
    void SetDamage(float newDamage) override { Damage = newDamage; }
    /// <summary>Sets the alignment and re-registers the sensor contact under it.</summary>
    void SetAlignment(int32_t newAlignment) override;
    void SetCommanderId(int32_t commanderId) override {}
    int32_t GetCommanderId() override { return -1; }
    /// <summary>A building or a tree building.</summary>
    int IsBuilding() override
    {
        return ObjectClass == MCObjectClass::Building || ObjectClass == MCObjectClass::TreeBuilding;
    }

    /// <summary>A mech's weight class from its tonnage.</summary>
    MCMechClass GetMechClass() override;
    int InTransport() override { return 0; }
    int IsCaptureable() override { return 0; }
    int IsPrison() override { return 0; }
    int GetAwake() override { return Flags & 1; }
    int GetExists() override { return (Flags >> 1) & 1; }
    int GetExistsAndAwake() override { return (Flags & 3) == 3; }
    void SetUseMe(int useMe) override
    {
        Flags &= ~4;

        if (useMe)
        {
            Flags |= 4;
        }
    }

    void SetExists(int exists) override
    {
        Flags &= ~2;

        if (exists)
        {
            Flags |= 2;
        }
    }

    void SetCaptured() override { Flags |= 8; }
    void ClearCaptured() override { Flags &= ~8; }
    int IsCaptured() override { return (Flags & 8) >> 3; }
    void SetTonnage(float newTonnage) override { Tonnage = newTonnage; }
    float GetTonnage() override { return Tonnage; }
    void SetCollisionFreeFrom(std::shared_ptr<MCGameObject> other) override { CollisionFreeFrom = std::move(other); }
    std::shared_ptr<MCGameObject> GetCollisionFreeFrom() override { return LockObject(CollisionFreeFrom); }
    void SetCollisionFreeTime(float time) override { CollisionFreeTime = time; }
    float GetCollisionFreeTime() override { return CollisionFreeTime; }
    void SetObjPosition(std::shared_ptr<MCObjectPosition> newObjPosition) override
    {
        ObjPosition = std::move(newObjPosition);
    }

    std::shared_ptr<MCObjectPosition> GetObjPosition() override { return ObjPosition.lock(); }
    void DamageObject(float damageAmount) override { Damage += damageAmount; }
    void SetExplDmg(float newDamage) override { ExplDamage = newDamage; }
    void SetExplRad(float newRadius) override { ExplRadius = newRadius; }
    float GetExplDmg() override { return ExplDamage; }
    void AddSalvage(uint8_t itemId, uint8_t numItems) override { Salvage.push_back({itemId, numItems}); }
    std::span<const MCSalvageItem> GetSalvage() override { return Salvage; }
    int32_t GetWindowsVisible() override { return WindowsVisible; }
    int32_t GetCurCV() override { return CurCV; }
    int32_t GetMaxCV() override { return MaxCV; }
    void SetCurCV(int32_t newCV) override { CurCV = newCV; }
    void IncrementAttackers() override { NumAttackers++; }
    /// <summary>One attacker fewer (asserts there was one).</summary>
    void DecrementAttackers() override;
    int32_t GetNumAttackers() override { return NumAttackers; }

    virtual int32_t SetTeam(std::shared_ptr<MCTeam> newTeam)
    {
        Team = std::move(newTeam);
        return 0;
    }

    /// <summary>How many of team <paramref name="teamId"/>'s sensors see the object.</summary>
    virtual int32_t GetContactCount(int32_t teamId);
    /// <summary>
    /// Fills <paramref name="vitalInfo"/> with what the status displays need (if not empty).
    /// </summary>
    /// <returns>The size of the record (0x19 here).</returns>
    virtual int32_t GetVitalInfo(std::span<uint8_t> vitalInfo);

    /// <summary>Tonnage (weight class of a mech).</summary>
    float Tonnage = 0;
    /// <summary>The object's record in the object map (<c>GameObjectMap</c>), or null.</summary>
    std::weak_ptr<MCObjectPosition> ObjPosition;
    /// <summary>The team the object belongs to.</summary>
    std::weak_ptr<MCTeam> Team;
    /// <summary>The object's entry in the potential contact manager, or null when it is no sensor contact.</summary>
    std::weak_ptr<MCPotentialContact> PotentialContact;
    /// <summary>Damage taken.</summary>
    float Damage = 0;
    /// <summary>The object this one may overlap without colliding, while <see cref="CollisionFreeTime"/> runs.</summary>
    std::weak_ptr<MCGameObject> CollisionFreeFrom;
    float CollisionFreeTime = 0;
    /// <summary>The object's screen position, computed by <see cref="OnScreen"/>.</summary>
    MCVector2D ScreenPos;
    /// <summary>Bit 0 awake, bit 1 exists, bit 2 use me, bit 3 captured (awake and use me to begin with).</summary>
    uint8_t Flags = 5;
    /// <summary>The turn the object was last on screen.</summary>
    int32_t WindowsVisible = 0;
    /// <summary>The radius of the object's explosion.</summary>
    float ExplRadius = 0;
    /// <summary>The damage the object's explosion does.</summary>
    float ExplDamage = 0;
    /// <summary>The salvage the object leaves, in the order it was added.</summary>
    std::vector<MCSalvageItem> Salvage;
    /// <summary>Maximum combat value.</summary>
    int32_t MaxCV = 0;
    /// <summary>Current combat value.</summary>
    int32_t CurCV = 0;
    /// <summary>The sensor blip's pose (BattleMech::render; <see cref="RenderSensorBlip"/>).</summary>
    int32_t BlipFrame = 0;
    /// <summary>The part of a pose the sensor blip has left over (<see cref="MCSensorBlipPoses"/>).</summary>
    float BlipTime = 0;
    /// <summary>How many movers are attacking the object.</summary>
    int32_t NumAttackers = 0;
};
