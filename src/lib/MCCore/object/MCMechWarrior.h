#pragma once

#include "abl/MCAblModule.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "object/MCPilotAlarm.h"
#include "object/MCPilotOrders.h"
#include "object/MCTacOrderQueue.h"

class MCFile;
class MCFitIniFile;
class MCGameObject;
class MCMover;
class MCMoverGroup;
class MCRadio;
enum class MCRadioMessageType : int32_t;
class MCScrollingTextWindow;
class MCTeam;
struct MCPathQueueRec;

/// <summary>Pilot skills (<see cref="SkillsTable"/> order).</summary>
inline constexpr int32_t NumSkills = 4;
/// <summary>The skills' indices: "Piloting", "Jumping", "Sensors", "Gunnery".</summary>
inline constexpr int32_t SkillPiloting = 0;
inline constexpr int32_t SkillJumping = 1;
inline constexpr int32_t SkillSensors = 2;
inline constexpr int32_t SkillGunnery = 3;

/// <summary>Which of the pilot's three order slots is current (<see cref="MCMechWarrior::OrderState"/>). Names are
/// MechCommander 2's.</summary>
enum class MCOrderState : int8_t
{
    General = 0,
    Player = 1,
    Alarm = 2,
    Count
};

/// <summary>An attacker a pilot remembers: who, and when it last attacked.</summary>
struct MCAttackerRec
{
    /// <summary>The attacker's part id.</summary>
    uint32_t AttackerId = 0;
    /// <summary>Scenario time of the last attack.</summary>
    float LastTime = 0;
};

/// <summary>A brain memory cell: an integer or a real ("MemType" 0 or 1).</summary>
union MCMemoryCell
{
    int32_t Integer;
    float Real;
};

/// <summary>
/// A pilot: name and portrait, skills and personality, wounds, the ABL brain, its orders (general, player and alarm,
/// the current one and the queue), move and attack orders, and the radio. Drives its vehicle through the decision
/// trees.
/// </summary>
/// <remarks>Original source: <c>object\warrior.cpp</c>, <c>object\warrior.h</c>. Field names are the port's where
/// the original's aren't known (MechCommander 2's where the layout matches).</remarks>
class MCMechWarrior : public std::enable_shared_from_this<MCMechWarrior>
{
public:
    /// <summary>Attackers a pilot remembers; more are not. Kept: the brains copy them into ABL arrays of their own
    /// (getattackers) sized for this many.</summary>
    static constexpr int32_t MaxAttackers = 50;
    /// <summary>Brain memory cells: the brain parameter files and the brains' getmemory calls number them.</summary>
    static constexpr int32_t NumMemoryCells = 50;

    /// <summary>
    /// A pilot with no orders and the default personality (40), its updates staggered by the number of pilots alive,
    /// and its two move paths.
    /// </summary>
    MCMechWarrior();
    ~MCMechWarrior();
    MCMechWarrior(const MCMechWarrior&) = delete;
    MCMechWarrior& operator=(const MCMechWarrior&) = delete;

    /// <summary>Drops the brain and its callbacks.</summary>
    void Lobotomy();
    /// <summary>
    /// Reads the pilot file: name, picture, callsign, audio/video and radio, paint scheme, personality, skills
    /// (current, original, latest, points), wounds; rolls whether the pilot survives ejection.
    /// </summary>
    /// <returns>0, or the FIT error of the first entry missing.</returns>
    int32_t Load(MCFitIniFile& warriorFile);
    /// <summary>
    /// Plays radio message <paramref name="messageId"/> when the pilot is under the local player's command
    /// (skipping repeats too soon after the last); a server passes others' messages on as radio chunks.
    /// </summary>
    void RadioMessage(int32_t messageId, int propogateIfMultiplayer);
    /// <summary>Plays radio message <paramref name="type"/> (as the numbered form).</summary>
    void RadioMessage(MCRadioMessageType type, int propogateIfMultiplayer)
    {
        RadioMessage(std::to_underlying(type), propogateIfMultiplayer);
    }

    /// <summary>The aggressiveness; halfway to 100 while on a combat order when <paramref name="current"/>.</summary>
    int32_t GetAggressiveness(int current) const;
    /// <summary>Queues a player order (0; 2 when full); starts it when it is the only one.</summary>
    int32_t AddQueuedTacOrder(MCTacticalOrder tacOrder);
    /// <summary>Takes the first queued order (0; 2 when empty).</summary>
    int32_t RemoveQueuedTacOrder(MCTacticalOrder& tacOrder);
    /// <summary>Reads the first queued order without taking it (0; 2 when empty).</summary>
    int32_t PeekQueuedTacOrder(MCTacticalOrder& tacOrder) const;
    void ClearTacOrderQueue();
    /// <summary>Makes the next queued order the player order.</summary>
    void ExecuteTacOrderQueue();
    /// <summary>The player order from the queue (when one runs), then the queued orders.</summary>
    std::vector<MCQueuedTacOrder> GetTacOrderQueue();
    /// <summary>How many orders <see cref="GetTacOrderQueue"/> gives.</summary>
    int32_t GetTacOrderQueueSize() const;
    /// <summary>
    /// On a client: drops the queued orders the server has executed (those before <paramref name="tacOrderId"/>,
    /// or the last executed one when 0).
    /// </summary>
    void UpdateClientOrderQueue(int32_t tacOrderId);
    std::shared_ptr<MCMoverGroup> GetGroup() const;
    std::shared_ptr<MCMover> GetPoint() const;
    int OnHomeTeam() const;
    /// <summary>Whether the vehicle belongs to a player (its net player id is set).</summary>
    int UnderHomeCommand() const;
    /// <summary>Rolls a skill check (counting the try and a success); returns the margin, negative on a failure.</summary>
    int32_t CheckSkill(int32_t skillId, float factor);
    /// <summary>Adds wounds; at 6 the pilot dies (or ejects when <paramref name="checkEject"/> and the roll allowed
    /// it) and the vehicle is disabled.</summary>
    int Injure(float numWounds, int checkEject);
    /// <summary>Ejects: a wound, the radio call, status 3 (4 if that killed him), the vehicle disabled.</summary>
    void Eject();
    void SetTeam(std::shared_ptr<MCTeam> newTeam);
    void SetVehicle(std::shared_ptr<MCGameObject> newVehicle);
    /// <summary>The vehicle piloted, null once it left the world.</summary>
    std::shared_ptr<MCGameObject> GetVehicle() const;
    /// <summary>The team, null without one.</summary>
    std::shared_ptr<MCTeam> GetTeam() const { return Team.lock(); }
    void SetBrainName(std::string_view brainName) { BrainStr = brainName; }
    /// <summary>Replaces the brain with a new module of <paramref name="brainHandle"/> and finds its alarm handlers.</summary>
    int32_t SetBrain(int32_t brainHandle);
    int32_t RunBrain();
    /// <summary>The vehicle's status, -1 without one.</summary>
    int32_t GetVehicleStatus() const;
    void UpdateAttackerStatus(uint32_t attackerId, float time);
    /// <summary>What is remembered of attacker <paramref name="attackerId"/>, if anything.</summary>
    std::optional<MCAttackerRec> GetAttackerInfo(uint32_t attackerId) const;
    /// <summary>The ids of the attackers of the last <paramref name="seconds"/>.</summary>
    std::vector<uint32_t> GetAttackers(float seconds) const;
    int32_t SetAttackTarget(std::shared_ptr<MCGameObject> object);
    /// <summary>The last target while it stays worth shooting; else clears it (and a combat order).</summary>
    std::shared_ptr<MCGameObject> GetLastTarget();
    /// <summary>
    /// Sets the last target (moving the vehicle's attacker count to it when the vehicle belongs to a player), and
    /// whether a disabled target still counts (<paramref name="obliterate"/>) and to conserve ammo.
    /// </summary>
    void SetLastTarget(std::shared_ptr<MCGameObject> target, int obliterate = 0, int conserveAmmo = 0);
    void SetCurrentTarget(std::shared_ptr<MCGameObject> target);
    void ClearAttackOrders();
    void ClearMoveOrders();
    /// <summary>Sets the move goal: <paramref name="type"/> -1 none, 0 a location, else an object's part id.</summary>
    int32_t SetMoveGoal(uint32_t type, MCVector3D& location, std::shared_ptr<MCGameObject> obj);
    /// <summary>Clears the move goal (<see cref="SetMoveGoal"/> with type -1).</summary>
    int32_t ClearMoveGoal();
    void PausePath();
    void ResumePath();
    /// <summary>
    /// The vehicle reached the end of its path: asks for the next leg (a queued move point or the next global step),
    /// or ends the move order and raises the no-path alarm.
    /// </summary>
    void ReachedPathEnd();
    float GetMoveDistanceLeft();
    int IsJumping(MCVector3D& jumpGoal) const;
    /// <summary>Whether the vehicle is in a jump.</summary>
    int IsJumping() const
    {
        MCVector3D jumpGoal;
        return IsJumping(jumpGoal);
    }

    /// <summary>The path being walked; when it is done, swaps in the next leg.</summary>
    MCMovePath& GetMovePath();
    /// <summary>
    /// Asks for the move path, which can advance it (see <see cref="GetMovePath"/>), where the original tested it
    /// against null; always true.
    /// </summary>
    bool PollMovePath()
    {
        GetMovePath();
        return true;
    }

    /// <summary>Follows <paramref name="wayPath"/>'s points (back and forth when <paramref name="patrol"/>).</summary>
    void SetMoveWayPath(const MCWayPath& wayPath, int patrol);
    /// <summary>Drops the way points.</summary>
    void ClearMoveWayPath();
    void SetMoveGlobalPath(std::span<const MCGlobalPathStep> path);
    /// <summary>Queues a path request with the <see cref="PathManager()"/>.</summary>
    void RequestMovePath(int32_t selectionIndex, uint32_t moveParams, int32_t source);
    /// <summary>
    /// Plans the move to the goal: a local path when close (or no long-range movement), else a global path
    /// area by area, each leg planned as the previous one ends; escape paths and jumps too. Sets
    /// <see cref="LastMoveCalcErr"/> and raises the no-path alarm on a failure.
    /// </summary>
    int32_t CalcMovePath(int32_t selectionIndex, uint32_t moveParams, int32_t source);
    /// <summary>The first way point of a queued move order, if the next queued order is one.</summary>
    int GetNextWayPoint(MCVector3D& nextPoint, int incWayPoint) const;
    /// <summary>
    /// Port-only (MechBraking, HDStops, the server's move chunks): whether the move goes on past
    /// <paramref name="path"/>'s end: a next leg of a global path, a next path planned (other than
    /// <paramref name="path"/>), or a queued way point to go on to.
    /// </summary>
    bool MoveGoesOnPast(const MCMovePath& path) const;
    /// <summary>
    /// Per weapon, whether it can fire at the target now (the attack chance, or -1 not ready, -2 no ammo, -3 out
    /// of range, -4 not locked, -5 no chance); returns how many can, or -1 can't fire, -2 no target, -3 out of range.
    /// </summary>
    int32_t CalcWeaponsStatus(std::shared_ptr<MCGameObject> target, std::span<int32_t> weaponList,
                              std::optional<MCVector3D> targetPoint) const;
    /// <summary>Fires what can fire at the last target; out of ammo ends the order.</summary>
    int32_t CombatDecisionTree();
    /// <summary>A point away from the enemy, walking the escape vector until off the map or blocked.</summary>
    MCVector3D CalcWithdrawGoal(float withdrawRange) const;
    /// <summary>Whether the vehicle is on, or its path crosses, a blown bridge.</summary>
    int MovingOverBlownBridge();
    /// <summary>Times out moves, reroutes around gates and blown bridges, and keeps attack moves in range.</summary>
    int MovementDecisionTree();
    /// <summary>Ends the current order; with <paramref name="updateTacOrder"/> the next order (player, then
    /// general) starts.</summary>
    void ClearCurTacOrder(int updateTacOrder, int updateBrain);
    void SetCurTacOrder(MCTacticalOrder tacOrder);
    void SetGeneralTacOrder(MCTacticalOrder tacOrder);
    void SetPlayerTacOrder(MCTacticalOrder tacOrder, int fromQueue);
    /// <summary>Sets the alarm order unless one of higher priority is waiting.</summary>
    void SetAlarmTacOrder(MCTacticalOrder tacOrder, int32_t priority);
    /// <summary>Raises an alarm (up to <see cref="MCPilotAlarm::MaxTriggers"/> triggers); -1 when full.</summary>
    int32_t TriggerAlarm(MCPilotAlarmType alarm, uint32_t triggerId);
    /// <summary>Handles an alarm at once (awake vehicles only): the built-in handler, then the brain's.</summary>
    int32_t HandleAlarm(MCPilotAlarmType alarm, uint32_t triggerId);
    /// <summary>Copies the alarm's triggers into <paramref name="triggerList"/>; returns how many.</summary>
    int32_t GetAlarmTriggers(MCPilotAlarmType alarm, std::span<uint32_t> triggerList);
    void ClearAlarm(MCPilotAlarmType alarm) { AlarmOf(alarm).NumTriggers = 0; }
    /// <summary>The alarm of type <paramref name="alarm"/>.</summary>
    MCPilotAlarm& AlarmOf(MCPilotAlarmType alarm) { return Alarm[static_cast<size_t>(std::to_underlying(alarm))]; }
    /// <summary>Handles each raised alarm (built-in handler, then the brain's) and clears it.</summary>
    int32_t CheckAlarms();
    /// <summary>
    /// The per-frame update the AI and network controls call: order status, weapons status, the brain, alarms,
    /// the next order, then the combat and movement trees.
    /// </summary>
    int32_t MainDecisionTree();
    /// <summary>A captured vehicle drops its orders; else the combat and movement trees run.</summary>
    void UpdateActions();
    /// <summary>Prints <paramref name="text"/> in the ABL debugger, if one is up (stopping in it with
    /// <paramref name="debugMode"/>).</summary>
    static void DebugPrint(std::string_view text, int debugMode);
    void DebugOrders();
    int32_t OrderWait(int unitOrder, MCOrderOrigin origin, int32_t seconds, int clearLastTarget);
    int32_t OrderStop(int unitOrder, int setTacOrder);
    int32_t OrderMoveToPoint(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCVector3D location,
                             int32_t selectionIndex, uint32_t params);
    int32_t OrderMoveToObject(int unitOrder, int setTacOrder, MCOrderOrigin origin,
                              std::shared_ptr<MCGameObject> target, int32_t selectionIndex, uint32_t params);
    int32_t OrderJumpToPoint(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCVector3D location,
                             int32_t selectionIndex);
    int32_t OrderJumpToObject(int unitOrder, int setTacOrder, MCOrderOrigin origin,
                              std::shared_ptr<MCGameObject> target, int32_t selectionIndex);
    int32_t OrderTraversePath(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCWayPath& wayPath,
                              uint32_t params);
    int32_t OrderPatrolPath(int unitOrder, int setTacOrder, MCOrderOrigin origin, MCWayPath& wayPath);
    int32_t OrderPowerUp(int unitOrder, MCOrderOrigin origin);
    int32_t OrderPowerDown(int unitOrder, MCOrderOrigin origin);
    int32_t OrderAttackObject(int unitOrder, MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, int32_t type,
                              int32_t method, int32_t range, int32_t aimLocation, uint32_t params);
    int32_t OrderAttackPoint(int unitOrder, MCOrderOrigin origin, MCVector3D location, int32_t type, int32_t method,
                             int32_t range, uint32_t params);
    void SetAttackTargetPoint(MCVector3D location) { AttackOrders.TargetPoint = location; }
    MCVector3D GetAttackTargetPoint() const { return AttackOrders.TargetPoint; }
    int32_t OrderWithdraw(int unitOrder, MCOrderOrigin origin, MCVector3D location);
    int32_t OrderEject(int unitOrder, int setTacOrder, MCOrderOrigin origin);
    int32_t OrderUseFireRange(int32_t range);
    int32_t OrderRefit(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params);
    int32_t OrderGetFixed(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params);
    int32_t OrderLoadIntoCarrier(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params);
    int32_t OrderDeployElementals(MCOrderOrigin origin, uint32_t params);
    int32_t OrderCapture(MCOrderOrigin origin, std::shared_ptr<MCGameObject> target, uint32_t params);
    static int32_t HandleTargetOfWeaponFire();
    int32_t HandleHitByWeaponFire();
    int32_t HandleCollision();
    static int32_t HandleDamageTakenRate();
    int32_t HandleUnitMateDeath();
    static int32_t HandleFriendlyVehicleCrippled();
    static int32_t HandleFriendlyVehicleDestruction();
    int32_t HandleOwnVehicleIncapacitation(uint32_t cause);
    int32_t HandleOwnVehicleDestruction(uint32_t cause) const;
    int32_t HandleOwnVehicleWithdrawn();
    int32_t HandleMoraleBreak();
    /// <summary>Counts the kill, calls it in, and scores gunnery skill points (a tenth for an ally's).</summary>
    int32_t HandleKilledTarget();
    static int32_t HandleUnitMateFiredWeapon();
    int32_t HandlePlayerOrder();
    int32_t HandleNoMovePath();
    static int32_t HandleGateClosing();
    /// <summary>The rank from the weighted skill ranks (<see cref="SkillWeightings"/>, <see cref="WarriorRankScale"/>).</summary>
    void CalcRank();
    /// <summary>Reads section "Warrior<paramref name="warriorId"/>": the brain's memory cells and static variables.</summary>
    int32_t LoadBrainParameters(MCFitIniFile& brainFile, int32_t warriorId);

    /// <summary>The order waiting in slot <paramref name="state"/>.</summary>
    MCTacticalOrder& TacOrderOf(MCOrderState state) { return TacOrder[static_cast<size_t>(state)]; }
    /// <summary>Whether a new order waits in slot <paramref name="state"/>.</summary>
    int32_t& NewTacOrderReceivedOf(MCOrderState state) { return NewTacOrderReceived[static_cast<size_t>(state)]; }
    /// <summary>The weapon status <see cref="CalcWeaponsStatus"/> last gave weapon <paramref name="weapon"/> of the
    /// vehicle (0 before it ran).</summary>
    int32_t WeaponStatus(int32_t weapon) const
    {
        return weapon >= 0 && weapon < std::ssize(WeaponsStatus) ? WeaponsStatus[static_cast<size_t>(weapon)] : 0;
    }

    /// <summary>"Name".</summary>
    std::string Name;
    /// <summary>"Callsign".</summary>
    std::string Callsign;
    /// <summary>"Picture", default "pilotx.gif".</summary>
    std::string Picture;
    /// <summary>"pilotVideo".</summary>
    std::string VideoStr;
    /// <summary>"pilotAudio".</summary>
    std::string AudioStr;
    /// <summary>The brain's name.</summary>
    std::string BrainStr;
    /// <summary>The pilot's index in Scenario::warriors (selectwarrior / getwarriorstatus take it).</summary>
    int32_t Index = 0;
    /// <summary>"PaintScheme", -1 for none.</summary>
    int32_t PaintScheme = -1;
    /// <summary>Rank (calcRank).</summary>
    uint8_t Rank = 0;
    /// <summary>Current skills (<see cref="SkillsTable"/>).</summary>
    int8_t Skills[NumSkills] = {};
    /// <summary>"Professionalism" (40 by default).</summary>
    int8_t Professionalism = 40;
    /// <summary>"Decorum".</summary>
    int8_t Decorum = 40;
    /// <summary>"Aggressiveness".</summary>
    int8_t Aggressiveness = 40;
    /// <summary>"Courage".</summary>
    int8_t Courage = 40;
    /// <summary>The courage read ("Courage"), before morale changes it.</summary>
    int8_t BaseCourage = 0;
    /// <summary>"Wounds"; 6 is death.</summary>
    float Wounds = 0.0f;
    /// <summary>The pilot's status: 0 normal, 2 withdrawn, 3 ejected, 4 dead.</summary>
    int32_t Status = 0;
    /// <summary>Rolled at load: survives ejection.</summary>
    int32_t EscapesThruEjection = 0;
    /// <summary>When "under attack" was last called in (at most every 20 seconds).</summary>
    float LastUnderAttackTime = -1000.0f;
    /// <summary>"Weapons at 50%" was called in.</summary>
    int32_t Weapons50Sent = 0;
    /// <summary>"Weapons out" was called in.</summary>
    int32_t WeaponsOutSent = 0;
    /// <summary>When a sensor contact was last called in (at most every 15 seconds).</summary>
    float LastContactTime = -1000.0f;
    /// <summary>The last message type played.</summary>
    int32_t LastMessageType = 0;
    /// <summary>What Radio::playMessage returned for it.</summary>
    int32_t LastMessage = -1;
    /// <summary>When it played (repeats within 10 seconds are skipped).</summary>
    float LastMessageTime = 0.0f;
    /// <summary>"NotMineYet".</summary>
    int NotMineYet = 0;
    /// <summary>The team.</summary>
    std::weak_ptr<MCTeam> Team;
    /// <summary>The vehicle piloted.</summary>
    std::weak_ptr<MCGameObject> Vehicle;
    /// <summary>The team's alignment.</summary>
    int8_t Alignment = 0;
    /// <summary>Per skill: tries (two counters each; checkSkill counts the second). Mission statistics.</summary>
    int32_t NumSkillUses[NumSkills][2] = {};
    /// <summary>Per skill: successes.</summary>
    int32_t NumSkillSuccesses[NumSkills][2] = {};
    /// <summary>Kills by kind (mech class 1-4, 5 vehicles and turrets, 6 elementals; the second counter counts).</summary>
    int32_t NumKilled[7][2] = {};
    /// <summary>Enemy mechs rammed on an attack order (BattleMechType::handleCollision).</summary>
    int32_t NumRams = 0;
    /// <summary>Jumps landed on the jump order's target (BattleMechType::handleCollision).</summary>
    int32_t NumJumpAttacks = 0;
    /// <summary>Per skill: the skill as a float.</summary>
    float SkillRank[NumSkills] = {};
    /// <summary>"SkillPoints" per skill.</summary>
    float SkillPoints[NumSkills] = {};
    /// <summary>"OriginalSkills".</summary>
    int8_t OriginalSkills[NumSkills] = {};
    /// <summary>"LatestSkills".</summary>
    int8_t LatestSkills[NumSkills] = {};
    /// <summary>"DescIndex", -1 for none.</summary>
    int32_t DescIndex = -1;
    /// <summary>"NameIndex", -1 for none.</summary>
    int32_t NameIndex = -1;
    /// <summary>When a home-team pilot was last left without orders (-1 while ordered).</summary>
    float TimeOfLastOrders = -1.0f;
    /// <summary>The attackers remembered (at most <see cref="MaxAttackers"/>).</summary>
    std::vector<MCAttackerRec> Attackers;
    /// <summary>DefaultAttackRadius.</summary>
    float AttackRadius = 0.0f;
    /// <summary>The brain's memory cells (loadBrainParameters).</summary>
    MCMemoryCell Memory[NumMemoryCells] = {};
    /// <summary>The ABL brain.</summary>
    std::shared_ptr<MCAblModule> Brain;
    /// <summary>
    /// Per alarm, whether the brain has its handler (<see cref="PilotAlarmFunctionName"/>), which the alarm looks up
    /// by name when it runs. (The original kept the handlers' symbol pointers.)
    /// </summary>
    std::array<bool, NumPilotAlarms> BrainAlarmHandled{};
    /// <summary>Next brain run (staggered by warrior count, then every BrainUpdateFrequency).</summary>
    float BrainUpdateTime = 0.0f;
    /// <summary>Next combat update time.</summary>
    float CombatUpdateTime = 0.0f;
    /// <summary>Next movement update time.</summary>
    float MovementUpdateTime = 0.0f;
    /// <summary>Per weapon of the vehicle: its status against the target (<see cref="CalcWeaponsStatus"/>).</summary>
    std::vector<int32_t> WeaponsStatus;
    /// <summary>What <see cref="CalcWeaponsStatus"/> returned (-2 before it ran).</summary>
    int32_t WeaponsStatusResult = -2;
    /// <summary>Per order state: a new order is waiting in <see cref="TacOrder"/>.</summary>
    std::array<int32_t, static_cast<size_t>(MCOrderState::Count)> NewTacOrderReceived{};
    /// <summary>The waiting orders: general, player, alarm.</summary>
    std::array<MCTacticalOrder, static_cast<size_t>(MCOrderState::Count)> TacOrder;
    /// <summary>The last order started.</summary>
    MCTacticalOrder LastTacOrder;
    /// <summary>The current tactical order.</summary>
    MCTacticalOrder CurTacOrder;
    /// <summary>The alarms (<see cref="MCPilotAlarmType"/>).</summary>
    std::array<MCPilotAlarm, NumPilotAlarms> Alarm{};
    /// <summary>The priority of the waiting alarm order.</summary>
    int32_t AlarmPriority = 0;
    /// <summary>The player order came from the queue.</summary>
    int32_t PlayerOrderFromQueue = 0;
    /// <summary>Set while the queue is executing.</summary>
    bool TacOrderQueueExecuting = false;
    /// <summary>The queued player orders.</summary>
    MCTacOrderQueue QueuedOrders;
    /// <summary>The next tactical order id (1 to start with).</summary>
    int32_t NextTacOrderId = 1;
    /// <summary>The id of the last order the server executed.</summary>
    int32_t LastTacOrderId = 0;
    /// <summary>Movement orders.</summary>
    MCMoveOrders MoveOrders;
    /// <summary>Attack orders.</summary>
    MCAttackOrders AttackOrders;
    /// <summary>The fire range ordered (orderUseFireRange), -1 for none.</summary>
    float OrderFireRange = -1.0f;
    /// <summary>The fire odds ordered (orderUseFireOdds), -1 for none.</summary>
    float OrderFireOdds = -1.0f;
    /// <summary>The last target (getLastTarget).</summary>
    std::weak_ptr<MCGameObject> LastTarget;
    /// <summary>When it was set, -1 for none.</summary>
    float LastTargetTime = -1.0f;
    /// <summary>Keep shooting it when disabled.</summary>
    int32_t LastTargetObliterate = 0;
    /// <summary>It is on our side.</summary>
    int32_t LastTargetFriendly = 0;
    /// <summary>Conserve ammo on it (the attack becomes type 3).</summary>
    int32_t LastTargetConserveAmmo = 0;
    /// <summary>Which of <see cref="TacOrder"/> is current.</summary>
    MCOrderState OrderState = MCOrderState::General;
    /// <summary>The pending path request.</summary>
    std::weak_ptr<MCPathQueueRec> MovePathRequest;
    /// <summary>Debug flags.</summary>
    uint32_t DebugFlags = 0;
    /// <summary>The radio (the sound system's radio list owns it), null without one.</summary>
    std::weak_ptr<MCRadio> Radio;
    /// <summary>"OldPilot".</summary>
    uint8_t OldPilot = 0;
    /// <summary>"Ammo out" was called in.</summary>
    int32_t AmmoOutSent = 0;

    /// <summary>Pilots alive (a new pilot's updates are staggered by it).</summary>
    static int32_t NumWarriors;
};

/// <summary>The skill names as the pilot files spell them.</summary>
extern const std::array<std::string_view, NumSkills> SkillsTable;
/// <summary>The error of the last <see cref="MCMechWarrior::CalcMovePath"/>.</summary>
extern int32_t LastMoveCalcErr;
