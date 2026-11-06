#pragma once

#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearance.h"
#include "object/MCBigGameObject.h"
#include "object/MCControl.h"
#include "object/MCDynamics.h"
#include "object/MCMoverParts.h"
#include "object/MCStatusChunk.h"
#include "object/MCWeaponFireChunk.h"
#include "object/MCWeaponShotInfo.h"

class MCFitIniFile;
class MCMechWarrior;
class MCMoverGroup;
class MCObjectEvent;
class MCSensorSystem;
class MCTeam;
struct MCSystemTracker;
enum class MCRadioMessageType : int32_t;

/// <summary>
/// Anything that moves under a pilot: mechs, ground vehicles, elementals. Holds the body, armor and inventory, the
/// sensors and weapons, the pilot, control and dynamics, path locks, and the network chunks.
/// </summary>
/// <remarks>Original source: <c>object\mover.cpp</c>, <c>object\mover.h</c>. Field names are the port's where the
/// original's aren't known; many follow MechCommander 2's Mover.</remarks>
class MCMover : public MCBigGameObject
{
public:
    /// <summary>The lowest part id of a mover; the scenario's mover roster is indexed from it.</summary>
    static constexpr int32_t FirstPartId = 0x200;
    /// <summary>One past the highest part id of a mover (the roster's size).</summary>
    static constexpr int32_t EndPartId = 0x1000;
    /// <summary>Weapon fire and critical hit chunks a mover queues per direction: a packet's chunk list (wire
    /// format).</summary>
    static constexpr int32_t MaxWeaponFireChunks = 0x80;
    /// <summary>Radio chunks a mover queues per direction: a packet's chunk list (wire format); more are dropped.</summary>
    static constexpr int32_t MaxRadioChunks = 7;
    /// <summary>Anti-missile systems a mover carries: the mech file stores this many slots (data format).</summary>
    static constexpr int32_t MaxAntiMissileSystems = 16;

    /// <summary>Every field at its starting value; in multiplayer, the network name's buffer.</summary>
    MCMover();
    /// <summary>Gives back the sensors, the jammer and ECM trackers and the potential contact.</summary>
    ~MCMover() override;

    using MCBigGameObject::Init;
    std::shared_ptr<MCAppearance> GetAppearance() override { return Appearance; }
    int32_t HandleEvent(MCObjectEvent& event) override;
    /// <summary>Zero.</summary>
    MCVector3D GetPositionFromHS(uint32_t hotSpot) override;
    int UnderPlayerControl() override { return 1; }
    /// <summary>The group's id, -1 for none.</summary>
    int32_t GetGroupId() override;
    /// <summary>Wakes or puts to sleep; a shut-down mover's pilot is ordered to power up when woken.</summary>
    void SetAwake(int awake) override;
    /// <summary>Sets the part id and registers the mover in the scenario's mover roster.</summary>
    void SetPartId(int32_t newPartId) override;
    void ReduceAntiMissileAmmo(int32_t numShots) override;
    int32_t FireAntiMissileSystem(int32_t numMissiles, int32_t& antiMissileShots) override;
    MCVector3D RelativePosition(float angle, float distance, uint32_t flags) override;
    void SetPosition(MCVector3D& newPosition) override;
    MCVector3D GetVelocity() override { return Velocity; }
    void SetVelocity(MCVector3D& newVelocity) override { Velocity = newVelocity; }
    MCFrameOfRef GetFrame() override { return Frame; }
    void SetFrame(MCFrameOfRef& newFrame) override { Frame = newFrame; }
    /// <summary>Sets the alignment, and the pilot's.</summary>
    void SetAlignment(int32_t newAlignment) override;
    void SetCommanderId(int32_t newCommanderId) override { CommanderId = static_cast<int8_t>(newCommanderId); }
    int32_t GetCommanderId() override { return CommanderId; }
    int LineOfSight(MCGameObject& target) override;
    int LineOfSight(MCVector3D point) override;
    int LineOfFire(MCGameObject& target) override;
    float RelFacingTo(MCVector3D goal, int32_t bodyPart) override;
    float RelViewFacingTo(MCVector3D goal) override;
    /// <summary>Deselecting keeps the selection a second longer (unless networked); selecting sets it.</summary>
    void SetSelected(int32_t newSelected) override;
    /// <summary>Whether the pilot's current order is a withdraw.</summary>
    int IsWithdrawing() override;
    std::shared_ptr<MCMechWarrior> GetPilot() override { return Pilot.lock(); }
    int IsRevealed() override;
    /// <summary>Joins <paramref name="newTeam"/>: sensors, jammer and ECM follow.</summary>
    int32_t SetTeam(std::shared_ptr<MCTeam> newTeam) override;
    int32_t GetVitalInfo(std::span<uint8_t> vitalInfo) override;

    /// <summary>Part id 0x200 + (commander * 32 + group) * 12 + index.</summary>
    virtual void SetPartId(int32_t commanderId, int32_t groupId, int32_t index);
    virtual int32_t GetSpeedState() { return 0; }
    /// <summary>The slope under the mover, in degrees.</summary>
    virtual float GetTerrainAngle();
    virtual float GetVelocityTilt();
    virtual int LineOfFire(MCVector3D point);
    virtual void LineOfSensor(MCGameObject& target, int32_t& sensorResult, int32_t& losResult);
    /// <summary>Gives <paramref name="tacOrder"/> to the pilot (networked: through the server).</summary>
    virtual int32_t HandleTacticalOrder(MCTacticalOrder tacOrder, int32_t priority, int queuePlayerOrder);
    virtual void PilotingCheck(uint32_t situation, float modifier);
    virtual void ForcePilotingCheck();
    virtual int CanFireWeapons() { return 1; }
    /// <summary>Every DamageRateFrequency seconds: over 10 damage since the last check alarms the pilot.</summary>
    virtual void UpdateDamageTakenRate();
    /// <summary>Returns 4 (no action). Unnamed in Ghidra; the name is MechCommander 2's.</summary>
    virtual int32_t CheckShortRangeCollision() { return 4; }
    virtual void SetOverlayWeightClass(int32_t overlayClass) { OverlayWeightClass = overlayClass; }
    virtual int32_t GetOverlayWeightClass() { return OverlayWeightClass; }
    virtual void GetStopInfo(float& stopHeading, float& stopVelocity) {}
    /// <summary>Whether a mover's path has locked the cell, or (diameter 3) any of the 3x3 cells around it;
    /// diameter 5 is never locked.</summary>
    virtual int GetPathLocked(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC, int32_t diameter);
    /// <summary>Locks or unlocks the cell, or (diameter 3) the 3x3 cells around it; diameter 5 does nothing.</summary>
    virtual void SetPathLock(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC, int set, int32_t diameter);
    /// <summary>Whether the cell's neighbour in <paramref name="dir"/> (adjCellTable) is path locked.</summary>
    virtual int GetAdjacentCellPathLocked(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC, int32_t dir);
    /// <summary>Locks (or unlocks) the mover's cell and its path's next cells.</summary>
    virtual void UpdatePathLock(int set);
    virtual int GetPathRangeLock(int32_t range, int& reachedEnd);
    /// <summary>
    /// Locks the next <paramref name="range"/> cells of the path (after unlocking the ones it held), stopping at a
    /// cell another mover holds (-1); with <paramref name="set"/> 0, unlocks them.
    /// </summary>
    virtual int32_t SetPathRangeLock(int set, int32_t range);
    virtual int GetPathRangeBlocked(int32_t range, int& reachedEnd);
    virtual int CrashAvoidanceSystem() { return 0; }
    virtual void UpdateHustleTime();
    virtual void MineCheck() {}
    virtual void UpdateMovement() {}
    virtual int32_t BounceToAdjCell();
    virtual int32_t SetGroup(std::shared_ptr<MCMoverGroup> newGroup);
    virtual void SetPilot(std::shared_ptr<MCMechWarrior> newPilot);
    virtual int32_t UpdateWeaponFireChunks(int32_t which);
    virtual int32_t UpdateCriticalHitChunks(int32_t which);
    virtual int32_t UpdateRadioChunks(int32_t which);
    virtual MCStatusChunk& GetStatusChunk() { return StatusChunk; }
    virtual int32_t BuildStatusChunk() { return 0; }
    virtual int32_t HandleStatusChunk(int32_t updateAge, uint32_t chunk) { return 0; }
    virtual MCMoveChunk& GetMoveChunk() { return MoveChunk; }
    virtual int32_t BuildMoveChunk() { return 0; }
    virtual int32_t HandleMoveChunk(uint32_t chunk) { return 0; }
    virtual int32_t CalcCV(int calcMax) { return 0; }
    virtual void GetDamageClass(int32_t& damageClass, int& shutDown);
    virtual int32_t GetBodyState() { return -1; }
    virtual float GetTotalEffectiveness() { return 0.0f; }
    virtual int32_t CalcOffsetMoveGoal(MCVector3D target, MCVector3D offset, MCVector3D& goal);
    virtual int32_t CalcMoveGoal(std::shared_ptr<MCGameObject> target, MCVector3D moveGoal, int32_t isGroup,
                                 int32_t offsetIndex, int32_t groupSize, int32_t pointIndex, MCVector3D& newGoal,
                                 uint32_t params);
    virtual int32_t CalcMovePath(const MCPathSearch& search, MCMovePath& path, MCVector3D start, int32_t thruArea,
                                 int32_t goalDoor, MCVector3D finalGoal, MCVector3D& goal,
                                 std::span<int32_t, 2> goalCell, uint32_t params);
    virtual int32_t CalcMovePath(const MCPathSearch& search, MCMovePath& path, int32_t pathType, MCVector3D start,
                                 MCVector3D goal, std::span<int32_t> goalCell, uint32_t params);
    virtual int32_t CalcEscapePath(const MCPathSearch& search, MCMovePath& path, MCVector3D start, MCVector3D goal,
                                   uint32_t params, MCVector3D& escapeGoal);
    virtual int32_t GetContacts(std::span<int32_t> contactList, int32_t contactCriteria, int32_t sortType);
    /// <summary>The weapon's facing to <paramref name="targetPosition"/>: relFacingTo of the point.
    /// Unnamed in Ghidra; the subclasses name it (their mangled names return float).</summary>
    virtual float WeaponLocked(int32_t weaponIndex, MCVector3D targetPosition);
    virtual int32_t WeaponInRange(int32_t weaponIndex, float metersToTarget);
    virtual int32_t GetWeaponsReady(std::span<int32_t> list, int32_t listSize);
    virtual int32_t GetWeaponsLocked(std::span<int32_t> list, int32_t listSize);
    virtual int32_t GetWeaponsInRange(std::span<int32_t> list, int32_t listSize, float orderFireRange);
    virtual int32_t GetWeaponShots(int32_t weaponIndex);
    virtual float GetWeaponAmmoLevel(int32_t weaponIndex);
    virtual void CalcWeaponEffectiveness(int setMax);
    virtual void CalcWeaponRangeRatings();
    /// <summary>Totals the rounds of each ammo type the weapons use (9999 for a weapon without ammo).</summary>
    virtual void CalcAmmoTotals();
    virtual int CalcOptimalRange(std::shared_ptr<MCGameObject> target);
    virtual int32_t CalcLongestRangeWeapon();
    virtual float GetFireRange(int32_t which);
    /// <summary>getFireRange(-2).</summary>
    virtual float GetMaxFireRange();
    virtual int IsWeaponIndex(int32_t itemIndex);
    virtual int IsWeaponReady(int32_t weaponIndex);
    virtual int IsWeaponWorking(int32_t weaponIndex);
    virtual int IsWeaponMissile(int32_t weaponIndex);
    /// <summary>Sets the weapon's ready time: now plus its recycle time. Unnamed in Ghidra; MechCommander 2's
    /// name.</summary>
    virtual void StartWeaponRecycle(int32_t weaponIndex);
    virtual int32_t TallyAmmo(int32_t ammoMasterId);
    virtual int32_t ReduceAmmo(int32_t ammoMasterId, int32_t amount);
    virtual int32_t GetNumAmmoTypes() { return NumAmmoTypes(); }
    virtual int32_t GetAmmoType(int32_t ammoTypeIndex) { return AmmoTypeTotal[ammoTypeIndex].MasterId; }
    virtual int32_t GetAmmoTypeTotal(int32_t ammoTypeIndex) { return AmmoTypeTotal[ammoTypeIndex].CurAmount; }
    virtual int32_t GetAmmoTypeStart(int32_t ammoTypeIndex) { return AmmoTypeTotal[ammoTypeIndex].StartAmount; }
    virtual void DeductWeaponShot(int32_t weaponIndex, int32_t ammoAmount);
    virtual int NeedsRefit(int armorOnly);
    virtual int32_t SortWeapons(std::span<int32_t> weaponList, std::span<int32_t> valueList, int32_t listSize,
                                int32_t sortType, int skillCheck);
    virtual float CalcAttackChance(std::shared_ptr<MCGameObject> target, int32_t aimLocation, float targetTime,
                                   int32_t weaponIndex, float modifiers, int32_t& range,
                                   std::optional<MCVector3D> targetPoint);
    virtual float CalcAttackModifier(MCGameObject& target, int32_t weaponIndex, int skillCheck) { return 0.0f; }
    virtual int HitInventoryItem(int32_t itemIndex, int setupOnly) { return 0; }
    /// <summary>
    /// Disables the mover: the pilot is alarmed; an enemy mech may leave salvage (MechSalvageChance) or be blown
    /// apart. Unnamed in Ghidra; GroundVehicle names its override disable.
    /// </summary>
    virtual void Disable(uint32_t cause);
    virtual void ShutDown();
    virtual void StartUp();
    virtual void DestroyBodyLocation(int32_t location) {}
    virtual void CalcCriticalHit(int32_t hitLocation) {}
    virtual int InjureBodyLocation(int32_t bodyLocation, float damage) { return 0; }
    virtual void AmmoExplosion(int32_t ammoIndex);
    virtual int32_t FireWeapon(std::shared_ptr<MCGameObject> target, float targetTime, int32_t weaponIndex,
                               int32_t attackType, int32_t aimLocation, std::optional<MCVector3D> targetPoint)
    {
        return 0;
    }

    virtual int32_t HandleWeaponFire(int32_t weaponIndex, std::shared_ptr<MCGameObject> target,
                                     std::optional<MCVector3D> targetPoint, int hit, float entryAngle,
                                     int32_t numMissiles, int32_t missilesPastAMS, int32_t antiMissileShots,
                                     int32_t hitLocation)
    {
        return 0;
    }

    virtual float RelFacingDelta(MCVector3D goalPos, MCVector3D targetPos);
    virtual int CanPowerUp() { return 1; }
    virtual int CanMove() { return 1; }
    virtual int CanJump() { return 0; }
    virtual float GetJumpRange(int32_t& numOffsets, int32_t& jumpCost);
    /// <summary>The jump range alone.</summary>
    float GetJumpRange()
    {
        int32_t numOffsets = 0;
        int32_t jumpCost = 0;
        return GetJumpRange(numOffsets, jumpCost);
    }

    virtual int IsJumping(MCVector3D& jumpGoal) { return 0; }
    /// <summary>Whether the mover is in a jump.</summary>
    int IsJumping()
    {
        MCVector3D jumpGoal;
        return IsJumping(jumpGoal);
    }

    /// <summary>The firing arc of the mover's class (mech FireArc, vehicle, elemental; else 60).</summary>
    virtual float GetFireArc();
    virtual float CalcMaxSpeed() { return 0.0f; }
    virtual float CalcSlowSpeed() { return 0.0f; }
    /// <summary>Unnamed in Ghidra; BattleMech names its override calcModerateSpeed.</summary>
    virtual float CalcModerateSpeed() { return 0.0f; }
    virtual int32_t CalcSpriteSpeed(float speed, uint32_t flags, int32_t& state, int32_t& throttle);
    virtual float GetGestureStopDistance() { return 0.0f; }
    virtual int HandleEjection() { return 0; }
    virtual float CalcExpectedTargetDamage() { return 0.0f; }
    virtual std::string_view GetIfaceName() { return "No Name"; }

    void SetLastValidPosition(MCVector3D pos) { LastValidPosition = pos; }
    /// <summary>The group's point, or null without a group. Unnamed in Ghidra; MechCommander 2's name.</summary>
    std::shared_ptr<MCMover> GetPoint() const;
    /// <summary>Packs and queues a weapon fire chunk (Fatal past <see cref="MaxWeaponFireChunks"/>).</summary>
    int32_t AddWeaponFireChunk(int32_t which, MCWeaponFireChunk& chunk);
    int32_t AddWeaponFireChunks(int32_t which, std::span<const uint32_t> packedChunks);
    /// <summary>Takes up to <paramref name="packedChunks"/>'s size queued chunks into it.</summary>
    /// <returns>How many.</returns>
    int32_t GrabWeaponFireChunks(int32_t which, std::span<uint32_t> packedChunks);
    int32_t ClearCriticalHitChunks(int32_t which);
    int32_t AddCriticalHitChunk(int32_t which, int32_t bodyLocation, int32_t criticalSpace);
    int32_t AddCriticalHitChunks(int32_t which, std::span<const uint8_t> packedChunks);
    /// <summary>Copies the queued chunks into <paramref name="packedChunks"/> (room for
    /// <see cref="MaxWeaponFireChunks"/>), leaving them queued.</summary>
    int32_t GrabCriticalHitChunks(int32_t which, std::span<uint8_t> packedChunks);
    int32_t ClearRadioChunks(int32_t which);
    int32_t AddRadioChunk(int32_t which, uint8_t msg);
    int32_t AddRadioChunks(int32_t which, std::span<const uint8_t> packedChunks);
    /// <summary>Copies the queued chunks into <paramref name="packedChunks"/> (room for
    /// <see cref="MaxRadioChunks"/>), leaving them queued.</summary>
    int32_t GrabRadioChunks(int32_t which, std::span<uint8_t> packedChunks);
    void PlayMessage(MCRadioMessageType messageId, int propogateIfMultiplayer) const;
    int32_t GetInventoryDamage(int32_t itemIndex);
    float GetProbeEffect();
    /// <summary>MaxVisualRadius plus the probe's effect.</summary>
    float GetVisualRange();
    /// <summary>The mover challenging this one; dropped once disabled.</summary>
    std::shared_ptr<MCGameObject> GetChallenger();
    /// <summary>Unnamed in Ghidra; MechCommander 2's name.</summary>
    void SetChallenger(std::shared_ptr<MCGameObject> newChallenger) { Challenger = newChallenger; }

    /// <summary>
    /// Body location <paramref name="location"/> of <see cref="Body"/>. Debug builds check the index is one of the
    /// body locations: an armor location (a mech's rear torsos, 8..10) used as a body location reads past them. Map
    /// one through <c>MechArmorToBodyLocation</c> first.
    /// </summary>
    auto BodyAt(int32_t location) -> MCBodyLocation&
    {
        SDL_assert(location >= 0 && location < NumBodyLocations());
        return Body[static_cast<size_t>(location)];
    }

    /// <summary>Const <see cref="BodyAt"/>.</summary>
    auto BodyAt(int32_t location) const -> const MCBodyLocation&
    {
        SDL_assert(location >= 0 && location < NumBodyLocations());
        return Body[static_cast<size_t>(location)];
    }

    /// <summary>How many body locations.</summary>
    int32_t NumBodyLocations() const { return static_cast<int32_t>(Body.size()); }
    /// <summary>How many armor locations.</summary>
    int32_t NumArmorLocations() const { return static_cast<int32_t>(Armor.size()); }

    /// <summary>
    /// Whether <paramref name="hitLocation"/>, as a network chunk carries it, is one the mover's weapon-hit handler
    /// takes: for a mech one of its armor locations or -1 (no location), for a vehicle one of its armor locations;
    /// anything for the others, which don't look locations up.
    /// </summary>
    /// <remarks>Issue #25: the chunks come from another machine and can hold -2 to 13.</remarks>
    bool IsHitLocation(int32_t hitLocation) const
    {
        switch (ObjectClass)
        {
            case MCObjectClass::BattleMech:
            {
                return hitLocation >= -1 && hitLocation < NumArmorLocations();
            }

            case MCObjectClass::GroundVehicle:
            {
                return hitLocation >= 0 && hitLocation < NumArmorLocations();
            }

            default:
            {
                return true;
            }
        }
    }

    /// <summary>How many ammo types.</summary>
    int32_t NumAmmoTypes() const { return static_cast<int32_t>(AmmoTypeTotal.size()); }

    /// <summary>The ground's normal under the mover (getTerrainAngle, getVelocityTilt).</summary>
    MCVector3D TerrainNormal;
    /// <summary>Velocity in meters per second.</summary>
    MCVector3D Velocity;
    /// <summary>Orientation.</summary>
    MCFrameOfRef Frame;
    /// <summary>The mover's long name (the string table entry of its profile; GroundVehicle's
    /// getIfaceName returns it). Also printed by the debug status chunks.</summary>
    std::string DebugStatus;
    /// <summary>Profile "icon".</summary>
    std::string IconName;
    /// <summary>The type's "Chassis"; indexes TileThrottleMultiplier / OverlayThrottleMultiplier.</summary>
    uint8_t Chassis = 0;
    /// <summary>The type's "EndoSteel" (mechs).</summary>
    int32_t EndoSteel = 0;
    /// <summary>The type's "TonnageClass".</summary>
    float TonnageClass = 0.0f;
    /// <summary>The type's internal structure tonnage.</summary>
    float InternalStructureTonnage = 0.0f;
    /// <summary>Damage taken since the last check (updateDamageTakenRate).</summary>
    float DamageRateTally = 0.0f;
    /// <summary>Scenario time of the next damage rate check.</summary>
    float DamageRateCheckTime = 1.0f;
    /// <summary>All the damage taken (BattleMech::handleWeaponHit).</summary>
    float TotalDamageTaken = 0.0f;
    /// <summary>The body locations.</summary>
    std::vector<MCBodyLocation> Body;
    /// <summary>"Armor" "Type".</summary>
    uint8_t ArmorType = 0;
    /// <summary>"Armor" "Tonnage".</summary>
    float ArmorTonnage = 0.0f;
    /// <summary>The armor locations.</summary>
    std::vector<MCArmorLocation> Armor;
    /// <summary>The inventory: other items, then weapons, then ammo.</summary>
    std::vector<MCInventoryItem> Inventory;
    /// <summary>Items before the weapons.</summary>
    uint8_t NumOther = 0;
    /// <summary>Weapons.</summary>
    uint8_t NumWeapons = 0;
    /// <summary>Ammo bins.</summary>
    uint8_t NumAmmos = 0;
    /// <summary>Ammo by type (calcAmmoTotals).</summary>
    std::vector<MCAmmoTally> AmmoTypeTotal;
    /// <summary>The pilot (the scenario owns it).</summary>
    std::weak_ptr<MCMechWarrior> Pilot;
    /// <summary>The sensors (from the sensor system manager, given back when the mover goes).</summary>
    std::weak_ptr<MCSensorSystem> SensorSystem;
    /// <summary>The team's tracker of the mover's jammer.</summary>
    std::weak_ptr<MCSystemTracker> JammerTracker;
    /// <summary>The team's tracker of the mover's ECM.</summary>
    std::weak_ptr<MCSystemTracker> EcmTracker;
    /// <summary>Inventory index of the cockpit, 0xff for none.</summary>
    uint8_t Cockpit = 0xff;
    /// <summary>Inventory index of the engine.</summary>
    uint8_t Engine = 0xff;
    /// <summary>Inventory index of the life support.</summary>
    uint8_t LifeSupport = 0xff;
    /// <summary>Inventory index of the sensors.</summary>
    uint8_t Sensor = 0xff;
    /// <summary>Inventory index of the ECM.</summary>
    uint8_t Ecm = 0xff;
    /// <summary>Inventory index of the probe.</summary>
    uint8_t Probe = 0xff;
    /// <summary>Inventory index of the jammer (setTeam adds it to the team's jammers).</summary>
    uint8_t Jammer = 0xff;
    /// <summary>The largest minimum range of the working weapons (calcLongestRangeWeapon).</summary>
    float MaxMinRange = 0.0f;
    /// <summary>The working weapon with the longest long range, 0xff for none.</summary>
    uint8_t LongestRangeWeapon = 0;
    /// <summary>The working weapon with the shortest short range, 0xff for none.</summary>
    uint8_t ShortestRangeWeapon = 0;
    /// <summary>calcWeaponEffectiveness(max).</summary>
    float MaxWeaponEffectiveness = 0.0f;
    /// <summary>calcWeaponEffectiveness.</summary>
    float WeaponEffectiveness = 0.0f;
    /// <summary>calcOptimalRange; -1 until known.</summary>
    float OptimalRange = -1.0f;
    /// <summary>Anti-missile systems.</summary>
    int8_t NumAntiMissileSystems = 0;
    /// <summary>Their inventory indices.</summary>
    std::array<uint8_t, MaxAntiMissileSystems> AntiMissileSystem{};
    /// <summary>"Engine" "Tonnage".</summary>
    float EngineTonnage = 0.0f;
    /// <summary>"Engine" "Rating".</summary>
    uint32_t EngineRating = 0;
    /// <summary>When a disabled engine blows (5 s after the critical hit; -1 for never). A mover with one set no
    /// longer moves; destroyBodyLocation checks it too.</summary>
    float EngineBlowTime = -1.0f;
    /// <summary>"Engine" "MaxRunSpeed" (calcMovePath and calcEscapePath plan with it).</summary>
    float MaxRunSpeed = 0.0f;
    /// <summary>Set by shutDown.</summary>
    bool ShutDownThisFrame = false;
    /// <summary>Set by startUp.</summary>
    bool StartUpThisFrame = false;
    /// <summary>Set by disable.</summary>
    bool DisableThisFrame = false;
    /// <summary>The group (lance).</summary>
    std::weak_ptr<MCMoverGroup> Group;
    /// <summary>
    /// The mover's rank in its order (SortMoverList, the group's order spread; copied into
    /// TacticalOrder::selectionIndex); -1 for none.
    /// </summary>
    int32_t SelectionIndex = -1;
    /// <summary>The commander's id.</summary>
    int8_t CommanderId = 0;
    /// <summary>-1 at first; forcePilotingCheck raises it to 0.</summary>
    int32_t PilotCheckModifier = -1;
    /// <summary>Cleared by pilotingCheck.</summary>
    bool PilotingCheckPending = false;
    /// <summary>When calcWeaponEffectiveness last ran (the AI recomputes its optimal range against a target whose
    /// effectiveness is newer).</summary>
    float LastWeaponEffectivenessCalc = 0.0f;
    /// <summary>When calcOptimalRange last ran.</summary>
    float LastOptimalRangeCalc = 0.0f;
    /// <summary>The mover challenging this one (getChallenger).</summary>
    std::weak_ptr<MCGameObject> Challenger;
    /// <summary>The appearance.</summary>
    std::shared_ptr<MCAppearance> Appearance;
    /// <summary>The control.</summary>
    std::unique_ptr<MCControl> Control;
    /// <summary>The dynamics.</summary>
    std::unique_ptr<MCDynamics> Dynamics;
    /// <summary>The DPID of the player whose mover this is (multiplayer; set when the scenario starts).</summary>
    uint32_t NetOwnerID = 0;
    /// <summary>Networked: the name (string 0xb9 to start with); empty in single player.</summary>
    std::string NetName;
    /// <summary>Networked: the owning player; -1 for none.</summary>
    int32_t NetPlayerId = -1;
    /// <summary>Networked: the roster slot; -1 for none.</summary>
    int32_t NetRosterIndex = -1;
    /// <summary>The network status chunk.</summary>
    MCStatusChunk StatusChunk;
    /// <summary>Set when a network move chunk arrives; the next update warps to its first step if too far off.</summary>
    bool NewMoveChunk = false;
    /// <summary>The network move chunk.</summary>
    MCMoveChunk MoveChunk{};
    /// <summary>Weapon fire chunks queued, out (0) and in (1).</summary>
    int32_t NumWeaponFireChunks[2] = {};
    std::array<std::array<uint32_t, MaxWeaponFireChunks>, 2> WeaponFireChunks{};
    /// <summary>Critical hit chunks queued.</summary>
    int32_t NumCriticalHitChunks[2] = {};
    std::array<std::array<uint8_t, MaxWeaponFireChunks>, 2> CriticalHitChunks{};
    /// <summary>Radio chunks queued.</summary>
    int32_t NumRadioChunks[2] = {};
    std::array<std::array<uint8_t, MaxRadioChunks>, 2> RadioChunks{};
    /// <summary>Whether the pilot was ordered to eject (sent in the status chunk).</summary>
    int32_t EjectOrderGiven = 0;
    /// <summary>
    /// Seconds left of the death sequence, counted down once the mover lies dead (0.8 for a mech or elemental): the
    /// death explosion goes off under 0.4, the wreck is left under 0.
    /// </summary>
    float DeathTimer = 1.0f;
    /// <summary>Set once the death explosion has gone off.</summary>
    bool DeathExplosionDone = false;
    /// <summary>Set by the withdraw order: the mover leaves the scenario once off the map or off screen.</summary>
    bool Withdrawing = false;
    /// <summary>The last position the mover could stand on.</summary>
    MCVector3D LastValidPosition;
    /// <summary>The way a mech pivots toward a target: 0 or 1, 0xff to choose again (BattleMech::pivotTo).</summary>
    uint8_t PivotDirection = 0xff;
    /// <summary>When the mover last stood on a hustle overlay (updateHustleTime); -999 for never.</summary>
    float LastHustleTime = -999.0f;
    /// <summary>A ground vehicle type's "AmmoTruck"; 0 for the others.</summary>
    int32_t AmmoTruck = 0;
    /// <summary>Distance moved since the mover last marked what it sees (it marks again past
    /// Terrain::metersPerVertex).</summary>
    float DistanceSinceMarkSeen = 0.0f;
    /// <summary>
    /// Port-only (Redux HD, mission/MCNightLights.h): whether the unit's headlight is on: the player's LIGHTS turns it
    /// off; the computer's units keep theirs on. It shines only at night while the unit runs.
    /// </summary>
    bool LightsOn = true;
    /// <summary>Port-only: <see cref="LitMarkedAt"/> for a light that hasn't marked since it came on.</summary>
    static constexpr float NeverLitMarked = -1.0f;
    /// <summary>
    /// Port-only: when (ActualTime) the unit's light last marked what it shows (MCNightLights::Mark), or
    /// <see cref="NeverLitMarked"/>.
    /// </summary>
    float LitMarkedAt = NeverLitMarked;
    /// <summary>Used by needsRefit and TacticalOrder::status.</summary>
    std::weak_ptr<MCGameObject> RefitBuddy;
    /// <summary>The type's "CrashAvoidSelf".</summary>
    int32_t CrashAvoidSelf = 1;
    int32_t CrashAvoidPath = 1;
    /// <summary>Path lock level of the mover's own cell (updatePathLock); the type's "CrashBlockSelf".</summary>
    int32_t PathLockLevel = 1;
    /// <summary>Path lock range (updatePathLock); the type's "CrashBlockPath".</summary>
    int32_t PathLockRange = 1;
    float CrashYieldTime = 1.5f;
    /// <summary>The cells setPathRangeLock has locked along the path.</summary>
    std::vector<MCPathRangeLock> PathRangeLocks;
    /// <summary>setOverlayWeightClass.</summary>
    int32_t OverlayWeightClass = 0;
    /// <summary>Scenario time a deselection takes effect (setSelected).</summary>
    float DeselectTime = 0.0f;
    /// <summary>Seconds <see cref="StationaryTarget"/> has held still (calcAttackChance).</summary>
    float StationaryTime = 0.0f;
    /// <summary>The target calcAttackChance last timed.</summary>
    std::weak_ptr<MCGameObject> StationaryTarget;
    /// <summary>The salvage roll (MechSalvageChance), -999 until rolled.</summary>
    int32_t SalvageRoll = -999;
    /// <summary>
    /// Set by the interface while the player holds a forced-order key (ctrl, F9-F12) with this mover selected;
    /// cleared on release. The mover's queued waypoints are then joined by lines.
    /// </summary>
    bool DrawOrderLines = false;
};

/// <summary>The target type of the weapon fire chunk being replayed (Mover::updateWeaponFireChunks).</summary>
extern int32_t TargetRolo;
/// <summary>A weapon fire chunk's entry angle quadrants in degrees: front, rear, left, right.</summary>
extern const std::array<float, 4> EntryAngleTable;
/// <summary>The weapon fire chunk being replayed (Mover::updateWeaponFireChunks).</summary>
extern MCWeaponFireChunk CurMoverWeaponFireChunk;

/// <summary>The mover with part id <paramref name="partId"/> (from the scenario's mover roster), or null.</summary>
std::shared_ptr<MCMover> GetMoverFromPartId(int32_t partId);
