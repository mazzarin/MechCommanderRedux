#pragma once

#include "object/MCMechGameSystem.h"
#include "object/MCMover.h"
#include "object/MCPilotOrders.h"

class MCFitIniFile;
class MCMovePath;
class MCSmoke;

/// <summary>A BattleMech: legs and torso, arms, jump jets, heat, and the mech's movement and combat.</summary>
/// <remarks>Original source: <c>object\mech.cpp</c>, <c>object\mech.h</c>. Its movement is in
/// <c>MCBattleMechMovement.cpp</c>, its weapons and damage in <c>MCBattleMechCombat.cpp</c>, its network chunks in
/// <c>MCBattleMechNetwork.cpp</c>.</remarks>
class MCBattleMech : public MCMover
{
public:
    /// <summary>Jump jet effects a jumping mech shows, one per jet (the .jmp file's offsets).</summary>
    static constexpr int32_t NumJumpJetEffects = 2;
    /// <summary>Smokes a mech streams from damaged equipment at once; a hit with all four streaming starts none (a
    /// rule of the look).</summary>
    static constexpr int32_t MaxSmokes = 4;

    /// <summary>
    /// Class BATTLEMECH; eight body locations and eleven armor locations; legs and torso intact; no jump; torso and
    /// arms straight.
    /// </summary>
    MCBattleMech();
    ~MCBattleMech() override;

    /// <summary>Copies the type's data, makes the dynamics and the MechActor appearance.</summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCMover::Init;
    int32_t Update() override;
    void Render() override;
    MCVector3D GetPositionFromHS(uint32_t hotSpot) override;
    /// <summary>
    /// Port-only (HD lighting's headlights): where the cockpit is (the hot spot after the weapons' second, the one the
    /// original ejects the pilot from, HandleEjection), as GetPositionFromHS places it, turned with the torso. Where the
    /// current gesture's packet doesn't hold it (the Commando's, cm.hsp's gestures 0-14), its offset in the first
    /// gesture's that does, at that gesture's first frame; none where no gesture's does.
    /// </summary>
    std::optional<MCVector3D> CockpitPosition();
    /// <summary>The cockpit's hot spot: this many after the weapons'.</summary>
    static constexpr uint32_t CockpitHotSpot = 1;
    void HandleStaticCollision() override;
    /// <summary>
    /// Reads the mech profile ("MechProfile"): name, battle rating, tonnage, names, pilot, icon, engine, movement
    /// system, armor, inventory and the body locations with their critical spaces.
    /// </summary>
    int32_t LoadProfile(MCFitIniFile& mechFile) override;
    int OnScreen() override;
    int32_t CalcHitLocation(std::optional<MCVector3D> attackerPosition, int32_t weaponIndex, int32_t attackSource,
                            int32_t attackType) override;
    int32_t HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) override;
    /// <summary>Replaces the control (1 player, 2 AI, 3 network) and gives it MechControlData.</summary>
    int32_t SetControl(uint32_t controlType, uint32_t controlData, int32_t controlParam) override;
    /// <summary>Whether a leg is gone (leg status 2 or 3).</summary>
    int IsCrippled() override;
    float RelFacingTo(MCVector3D goal, int32_t bodyPart) override;
    float RelViewFacingTo(MCVector3D goal) override;
    /// <summary>A home team mech flagged captureable and not destroyed.</summary>
    int IsCaptureable() override;
    int32_t GetVitalInfo(std::span<uint8_t> vitalInfo) override;
    /// <summary>From the appearance's gesture (mechSpeedStateArray).</summary>
    int32_t GetSpeedState() override;
    void PilotingCheck(uint32_t situation, float modifier) override;
    int CrashAvoidanceSystem() override;
    void MineCheck() override;
    void UpdateMovement() override;
    int32_t UpdateCriticalHitChunks(int32_t which) override;
    int32_t BuildStatusChunk() override;
    int32_t HandleStatusChunk(int32_t updateAge, uint32_t chunk) override;
    int32_t BuildMoveChunk() override;
    int32_t HandleMoveChunk(uint32_t chunk) override;
    int32_t CalcCV(int calcMax) override;
    /// <summary>From the appearance's gesture (MechStateByGesture).</summary>
    int32_t GetBodyState() override;
    float GetTotalEffectiveness() override;
    float WeaponLocked(int32_t weaponIndex, MCVector3D targetPosition) override;
    int IsWeaponReady(int32_t weaponIndex) override;
    int IsWeaponWorking(int32_t weaponIndex) override;
    float CalcAttackChance(std::shared_ptr<MCGameObject> target, int32_t aimLocation, float targetTime,
                           int32_t weaponIndex, float modifiers, int32_t& range,
                           std::optional<MCVector3D> targetPoint) override;
    int HitInventoryItem(int32_t itemIndex, int setupOnly) override;
    void DestroyBodyLocation(int32_t location) override;
    void CalcCriticalHit(int32_t hitLocation) override;
    int InjureBodyLocation(int32_t bodyLocation, float damage) override;
    int32_t FireWeapon(std::shared_ptr<MCGameObject> target, float targetTime, int32_t weaponIndex, int32_t attackType,
                       int32_t aimLocation, std::optional<MCVector3D> targetPoint) override;
    int32_t HandleWeaponFire(int32_t weaponIndex, std::shared_ptr<MCGameObject> target,
                             std::optional<MCVector3D> targetPoint, int hit, float entryAngle, int32_t numMissiles,
                             int32_t missilesPastAMS, int32_t antiMissileShots, int32_t hitLocation) override;
    int CanPowerUp() override;
    /// <summary>Unless both legs are gone (leg status 3).</summary>
    int CanMove() override;
    /// <summary>Whether the mech has jump jets.</summary>
    int CanJump() override;
    using MCMover::GetJumpRange;
    float GetJumpRange(int32_t& numOffsets, int32_t& jumpCost) override;
    using MCMover::IsJumping;
    int IsJumping(MCVector3D& jumpGoal) override;
    float CalcMaxSpeed() override;
    float CalcSlowSpeed() override;
    float CalcModerateSpeed() override;
    int32_t CalcSpriteSpeed(float speed, uint32_t flags, int32_t& state, int32_t& throttle) override;
    int HandleEjection() override;
    std::string_view GetIfaceName() override { return IfaceName.c_str(); }

    /// <summary>The heat of weapon <paramref name="weaponIndex"/> (its master component's).</summary>
    virtual float GetWeaponHeat(int32_t weaponIndex);
    /// <summary>A networked mech's movement: follows the move chunks.</summary>
    virtual void NetUpdateMovement();
    /// <summary>Plays a critical hit of a network chunk.</summary>
    virtual void HandleCriticalHit(int32_t bodyLocation, int32_t criticalSpace);
    /// <summary>The damage the mech's weapons deal against a still target in 10 seconds.</summary>
    virtual float CalcMaxTargetDamage();
    /// <summary>The damage the mech's weapons are expected to deal against <paramref name="target"/>.</summary>
    virtual float CalcExpectedTargetDamage(std::shared_ptr<MCGameObject> target);
    using MCMover::CalcExpectedTargetDamage;

    /// <summary>Sets and returns the leg status from the legs' damage (alarms / radio on change).</summary>
    int32_t CalcLegStatus();
    /// <summary>Sets and returns the torso status: 1 when the centre torso is damaged, else 0.</summary>
    int32_t CalcTorsoStatus();
    /// <summary>Drives a jump under way: on landing, back on the path.</summary>
    int UpdateJump();
    /// <summary>Pivots in place before moving; nonzero while turning.</summary>
    int PivotTo();
    /// <summary>Back to moving forward, unless pivoting with no path.</summary>
    void UpdateMoveStateGoal();
    /// <summary>
    /// Steers along the pilot's move path: advances the step once within the margin of error, then sets the
    /// gesture, throttle and turn (or asks for a pivot through <paramref name="newMoveState"/>). Nonzero once the
    /// path is done. The last two parameters are unused.
    /// </summary>
    int UpdateMovePath(char& newRotate, char& newThrottleSetting, float& newRotatePerSec, int32_t& newGestureStateGoal,
                       MCMoveState& newMoveState, int32_t& minThrottle, int32_t& maxThrottle);
    /// <summary>At the path's end: on to the next way point, or done moving.</summary>
    void SetNextMovePath(char& newThrottleSetting, int32_t& newGestureStateGoal);
    /// <summary>
    /// Port-only (MechBraking): fits the mech's stride and stop to end where its move does (MCMechActor::FitStop), and
    /// asks for the stand through <paramref name="newGestureStateGoal"/> when the stop is to start.
    /// </summary>
    void PlanStop(int32_t& newGestureStateGoal);
    /// <summary>
    /// The margin of error the move path's last step is reached within (the original's MoveMarginOfError[1]); port-only
    /// (MechBraking): wider while the mech brakes to a halt there.
    /// </summary>
    float LastStepMargin() const;
    /// <summary>
    /// Port-only (MechBraking): whether the mech's move ends where <paramref name="path"/> (its move path) does: no jump
    /// left on the way, and it goes on no further. On a multiplayer client, whose path is the server's latest steps,
    /// when the server's chunks showed the move's end (<see cref="ChunkShowedMoveEnd"/>).
    /// </summary>
    bool MoveEndsWith(const MCMovePath& path) const;
    /// <summary>
    /// Port-only (HDStops): whether the mech's move still has somewhere to go: its path paused with steps left (yielding
    /// to a unit in its way), a path asked for, or a next leg, path or way point. On a multiplayer client, whose path is
    /// the server's latest steps, unless the server's chunks showed the move's end (<see cref="ChunkShowedMoveEnd"/>).
    /// Reads the orders as they stand (MCMechWarrior::GetMovePath would move them on).
    /// </summary>
    bool MoveHeldUp() const;
    /// <summary>Turns the torso toward the target (or back to straight) at the dynamics' rate.</summary>
    void UpdateTorso(float newRotatePerSec);
    /// <summary>Applies the frame's requests to the control data.</summary>
    void SetControlSettings(char& newRotate, char& newThrottleSetting, float& newRotatePerSec,
                            int32_t& newGestureStateGoal, int32_t& minThrottle, int32_t& maxThrottle);
    /// <summary>A networked mech's <see cref="UpdateMovePath"/>, along the move chunk's steps.</summary>
    int NetUpdateMovePath(char& newRotate, char& newThrottleSetting, float& newRotatePerSec,
                          int32_t& newGestureStateGoal, MCMoveState& newMoveState, int32_t& minThrottle,
                          int32_t& maxThrottle);
    /// <summary>Starts the jump jet effects (and the launch crater).</summary>
    void CreateJumpFX();
    /// <summary>Ends the jump jet effects.</summary>
    void EndJumpFX();
    /// <summary>Where jump jet <paramref name="jet"/> (0 or 1) is this frame.</summary>
    MCVector3D GetJumpPosition(int32_t jet);
    /// <summary>Where a hit on the destroyed <paramref name="hitLocation"/> goes (MechTransferHitTable).</summary>
    static int32_t TransferHitLocation(int32_t hitLocation);
    /// <summary>Damages the ammunition and weapons loaded in the mech (a shutdown's heat).</summary>
    void DamageLoadedComponents();

    /// <summary>The weight class (getMechClass; 1 to start), indexing RankVersusChassisCombatModifier.</summary>
    uint8_t MechClass = 1;
    /// <summary>"ChassisBR" (100 when missing).</summary>
    int32_t ChassisBR = 0;
    /// <summary>0 intact, 2 a leg gone, 3 both legs gone (calcLegStatus).</summary>
    int8_t LegStatus = 0;
    /// <summary>calcTorsoStatus.</summary>
    int8_t TorsoStatus = 0;
    /// <summary>Inventory index of the left arm actuator.</summary>
    uint8_t LeftArmActuator = 0;
    /// <summary>Inventory index of the right arm actuator.</summary>
    uint8_t RightArmActuator = 0;
    /// <summary>Inventory index of the left leg actuator.</summary>
    uint8_t LeftLegActuator = 0;
    /// <summary>Inventory index of the right leg actuator.</summary>
    uint8_t RightLegActuator = 0;
    /// <summary>Inventory index of the gyro.</summary>
    uint8_t Gyro = 0;
    /// <summary>Jump jets fitted (canJump, getJumpRange).</summary>
    uint8_t NumJumpJets = 0;
    /// <summary>When the last jump started; -100 before any (updateJump).</summary>
    float JumpTime = -100.0f;
    /// <summary>Set while jumping.</summary>
    bool InJump = false;
    /// <summary>Where the jump lands.</summary>
    MCVector3D JumpGoal;
    /// <summary>When the center torso's internal structure was first injured (-1 before); destroyBodyLocation.</summary>
    float CenterTorsoInjuredTime = -1.0f;
    /// <summary>Set by a hit from behind (outside 90 degrees of the torso); a fall then goes forward (gesture 7).</summary>
    bool HitFromBehindThisFrame = false;
    /// <summary>Set by a hit from the front; a fall then goes backward (gesture 8). Both clear when the fall starts.</summary>
    bool HitFromFrontThisFrame = false;
    /// <summary>Set when the left arm is destroyed; the control turns it into MechControlData::blowLeftArm.</summary>
    bool LeftArmBlownThisFrame = false;
    /// <summary>Set when the right arm is destroyed; the control turns it into MechControlData::blowRightArm.</summary>
    bool RightArmBlownThisFrame = false;
    /// <summary>
    /// Port-only (HD art's arms): how far past what the right and left arms had left the hits that took them off went, a share
    /// of each arm's whole structure (0: gone with its side torso, to a critical, or not yet): MCDebris::StyleOfBlow.
    /// </summary>
    float RightArmBlow = 0.0f;
    float LeftArmBlow = 0.0f;
    /// <summary>The footprint of the gesture's second step (hot spot packet slot 4) is down; the walking gestures
    /// re-arm it once past that frame.</summary>
    bool SecondStepPrinted = false;
    /// <summary>The same for the first step (packet slot 0).</summary>
    bool FirstStepPrinted = false;
    /// <summary>Torso yaw in degrees, within the dynamics type's maxTorsoYaw.</summary>
    float TorsoRotation = 0.0f;
    /// <summary>Right arm yaw in degrees, within maxArmYaw.</summary>
    float RightArmRotation = 0.0f;
    /// <summary>Left arm yaw in degrees, within maxArmYaw.</summary>
    float LeftArmRotation = 0.0f;
    /// <summary>Latched once the dead mech's actor lies still; the death sequence (deathTimer) runs from then.</summary>
    bool LyingDead = false;
    /// <summary>Set once the wreck and its crater are left and the mech is off the interface.</summary>
    bool WreckDone = false;
    /// <summary>Smoke streaming from damaged equipment (hitInventoryItem); the mech owns it.</summary>
    std::array<std::shared_ptr<MCSmoke>, MaxSmokes> Smoke;
    /// <summary>The hot spot each smoke streams from.</summary>
    std::array<int32_t, MaxSmokes> SmokeHotSpot{};
    /// <summary>Seconds each smoke has left (15 at the start).</summary>
    std::array<float, MaxSmokes> SmokeTime{};
    /// <summary>The jump jet effects while jumping; the mech owns them.</summary>
    std::array<std::shared_ptr<MCGameObject>, NumJumpJetEffects> JumpFX;
    /// <summary>calcMaxTargetDamage.</summary>
    float MaxTargetDamage = 0.0f;
    /// <summary>The name the interface shows.</summary>
    std::string IfaceName;
    /// <summary>"Pilot" (-1 when missing).</summary>
    int32_t PilotId = 0;
    /// <summary>Whether the mech can be captured.</summary>
    bool Captureable = false;
    /// <summary>"NotMineYet" (1 when missing).</summary>
    bool NotMineYet = false;
    /// <summary>Set when a mine goes off under the mech; mineCheck then marks the next tile without its side's mine
    /// state (state 1) and clears it.</summary>
    bool SteppedOnMine = false;
    /// <summary>"DescIndex": the interface name is string 300 + it (-1 when missing).</summary>
    int32_t DescIndex = 0;
    /// <summary>"NameIndex".</summary>
    int32_t NameIndex = 0;
    /// <summary>"NameVariant".</summary>
    int32_t NameVariant = 0;
    /// <summary>
    /// Port-only (HDStops, MechBraking): on a multiplayer client, whether the server's latest move chunk with a step to go
    /// held the move's end. The server says so in a chunk with room left (MCChunkWord): the move ends there, or goes on
    /// (the mech held up, or its next path or leg yet to be planned). An older server's doesn't: a mech closing on its
    /// move's end is sent ever fewer steps, then its own cell; one held up is sent its own cell straight after a full
    /// chunk; so fewer steps than a chunk carries show the end, and a chunk of one cell keeps the last word (near the end
    /// it may be the step being walked into at a leg's end, or a wait, which then reads as there: the mech stands, as in
    /// the original). True for a mech yet to move.
    /// </summary>
    bool ChunkShowedMoveEnd = true;
    /// <summary>
    /// Port-only (MechBraking): on a multiplayer client, where the move ends as the server's chunks showed it (the end
    /// cell's middle, as its path's steps are): a stop is fitted to end there, as the chunks after it may hold only the
    /// step being walked into. None while the end isn't shown.
    /// </summary>
    std::optional<MCVector3D> ChunkMoveEnd;

private:
    /// <summary>
    /// A hot spot's place in the world: its offset (<paramref name="offsetX"/>, <paramref name="offsetY"/>,
    /// <paramref name="offsetZ"/>, the packet's units) from the mech's position, turned to <paramref name="turned"/>
    /// degrees snapped to the art's facings (GetPositionFromHS's).
    /// </summary>
    MCVector3D PlaceHotSpot(float offsetX, float offsetY, float offsetZ, double turned) const;
};
