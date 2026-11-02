#pragma once

#include "object/MCPotentialContact.h"

class MCGameObject;
class MCTeam;

/// <summary>
/// One object's sensors: a range (by the owner's speed and its pilot's sensor skill), scaled by the enemy's
/// jammers and ECM, and the contacts it currently holds. Pooled by the <see cref="MCSensorSystemManager"/>.
/// </summary>
/// <remarks>Original source: <c>object\contact.cpp</c>.</remarks>
class MCSensorSystem : public std::enable_shared_from_this<MCSensorSystem>
{
public:
    /// <summary>
    /// Sensor <paramref name="id"/>, with no owner, team or range; staggers its first scan by the sensors alive before
    /// it (0.1 s each, from 0.25 s); scans every ContactUpdateFrequency seconds.
    /// </summary>
    explicit MCSensorSystem(int32_t id);
    /// <summary>Counts itself out of <see cref="NumSensorsMade"/>.</summary>
    ~MCSensorSystem();
    MCSensorSystem(const MCSensorSystem&) = delete;
    MCSensorSystem& operator=(const MCSensorSystem&) = delete;

    /// <summary>
    /// Sets the range; a mover's is split into three by speed state, reduced by its pilot's sensor skill
    /// (SensorSkillMoveRange / SensorSkillMoveFactor).
    /// </summary>
    void SetRange(float newRange);
    /// <summary>
    /// The range now: a mover's for its speed state, eased over six turns when that changes; times the team
    /// multiplier.
    /// </summary>
    float GetSkilledRange();
    /// <summary>Leaves the old team's sensors and joins <paramref name="newTeam"/>'s (fatal for a team with no
    /// id).</summary>
    void SetTeam(std::shared_ptr<MCTeam> newTeam);
    /// <summary>
    /// Whether the sensors work: on a team, the owner alive and awake, and (for a mover) its sensor component
    /// present and undamaged.
    /// </summary>
    int Enabled() const;
    /// <summary>Drops every contact and leaves the team.</summary>
    void Disable();
    /// <summary>The weaker of <paramref name="team"/>'s jamming and its ECM at the owner's position.</summary>
    float CalcTeamEffect(MCTeam& team) const;
    /// <summary>Once per scenario time: each team's effect on these sensors, and the one that applies.</summary>
    void CalcTeamMultipliers();
    /// <summary>Adds <paramref name="contact"/> unless already held.</summary>
    void AddSensorContact(MCPotentialContact& contact);
    /// <summary>Drops the contact in slot <paramref name="index"/> (the last one fills the gap).</summary>
    void RemoveSensorContact(int32_t index);
    /// <summary>Drops <paramref name="contact"/> if held.</summary>
    void RemoveSensorContact(MCPotentialContact& contact);
    /// <summary>Drops every contact.</summary>
    void ClearSensorContacts();
    /// <summary>
    /// Re-checks the contacts held (once per scenario time): drops the disabled and those out of range. The
    /// original's name is lost.
    /// </summary>
    void UpdateContacts();
    /// <summary>
    /// When due (every scanFrequency seconds) or <paramref name="forceScan"/>: scans the battlefield, and has the
    /// pilot report new contacts.
    /// </summary>
    void UpdateScan(int forceScan);
    /// <summary>Scans list <paramref name="type"/> for other teams' objects; returns the newly sensed ones.</summary>
    int32_t ScanBattlefield(MCPotentialContactType type);
    /// <summary>Scans the lists the owner's alignment can see; returns the newly sensed.</summary>
    int32_t ScanBattlefield();
    /// <summary>
    /// Whether <paramref name="target"/> is on these sensors: in range, sensable, and (for a mover) the sensor
    /// working; a probe extends the range for hidden (status 5) targets.
    /// </summary>
    int OnSensors(MCGameObject& target);
    /// <summary>Contacts held.</summary>
    int32_t NumContacts() const { return static_cast<int32_t>(_Contacts.size()); }

    /// <summary>Index in the manager's pool (and in a contact's sensor slots).</summary>
    int32_t Id = 0;
    /// <summary>The object carrying the sensors.</summary>
    std::weak_ptr<MCGameObject> Owner;
    /// <summary>The owner's team.</summary>
    std::weak_ptr<MCTeam> Team;
    /// <summary>0 Inner Sphere, 1 clan, 2 allied; -1 for none.</summary>
    int32_t TeamIndex = -1;
    /// <summary>Slot in the team's sensor list (set by Team::addSensor); -1 when not on a team.</summary>
    int32_t TeamSensorSlot = -1;
    /// <summary>The base range; -1 for no sensors.</summary>
    float Range = -1.0f;
    /// <summary>A mover's range by speed state (still, moving, running).</summary>
    std::array<float, 3> SpeedRange{};
    /// <summary>The range last settled on (getSkilledRange eases toward the new one).</summary>
    float CurrentRange = 0.0f;
    /// <summary>The turn the easing ends; -1 when settled.</summary>
    int32_t RangeChangeTurn = -1;
    /// <summary>Each team's jamming/ECM effect on these sensors (Inner Sphere, clan, allied).</summary>
    std::array<float, 3> TeamMultiplier{1.0f, 1.0f, 1.0f};
    /// <summary>The effect that applies (the enemy's).</summary>
    float Multiplier = 1.0f;
    /// <summary>Scenario time of the next scan.</summary>
    float NextScanTime = 0.0f;
    /// <summary>Scenario time of the last scan.</summary>
    float LastScanTime = 0.0f;
    /// <summary>Scenario time calcTeamMultipliers last ran.</summary>
    float LastMultiplierTime = 0.0f;
    /// <summary>Seconds between scans (4).</summary>
    float ScanFrequency = 0.0f;
    /// <summary>Contacts newly sensed, over the mission.</summary>
    int32_t TotalContacts = 0;

    /// <summary>Sensors alive: the next one's first scan is staggered by it.</summary>
    /// <remarks>As in the original, it counts down as each mission's sensors are destroyed.</remarks>
    static int32_t NumSensorsMade;

private:
    /// <summary>The contacts held, by <c>MCPotentialContact::Id</c>.</summary>
    std::vector<uint16_t> _Contacts;
};

/// <summary>The pool of sensor systems.</summary>
/// <remarks>Original source: <c>object\contact.cpp</c>. Part of the <see cref="MCContactSystem"/>.</remarks>
class MCSensorSystemManager
{
public:
    /// <summary>Sensors made up front (the original's whole pool; more are made as needed).</summary>
    static constexpr int32_t InitialSensors = 0x41;

    /// <summary>Makes the first <see cref="InitialSensors"/> sensors, all free.</summary>
    MCSensorSystemManager();
    MCSensorSystemManager(const MCSensorSystemManager&) = delete;
    MCSensorSystemManager& operator=(const MCSensorSystemManager&) = delete;

    /// <summary>Takes a free sensor (the one freed last first, as in the original); makes one when none is free.</summary>
    std::shared_ptr<MCSensorSystem> NewSensor();
    /// <summary>Returns <paramref name="sensor"/> to the free ones.</summary>
    void FreeSensor(MCSensorSystem& sensor);
    /// <summary>Sensor <paramref name="id"/>.</summary>
    MCSensorSystem& Sensor(int32_t id) const { return *_Sensors[id]; }
    /// <summary>Sensors made.</summary>
    size_t Size() const { return _Sensors.size(); }

private:
    /// <summary>Makes the next sensor.</summary>
    std::shared_ptr<MCSensorSystem> MakeSensor();

    /// <summary>Every sensor, by id.</summary>
    std::vector<std::shared_ptr<MCSensorSystem>> _Sensors;
    /// <summary>The free sensors; the next taken is at the back.</summary>
    std::vector<int32_t> _Free;
};

/// <summary>Debug: every target is on every sensor.</summary>
extern int SensorAutomaticSuccess;
/// <summary>Sensor skill thresholds (45, 59, 69, 80) for SensorSkillMoveFactor's rows (the mover game system FIT's
/// "SensorSkillMoveRange").</summary>
extern char SensorSkillMoveRange[4];
/// <summary>Per skill row: the moving and running range factors (FIT "SensorSkillMoveFactor").</summary>
extern float SensorSkillMoveFactor[4][2];
/// <summary>Sensors "SensorModifiers" (loadMoverGameSystem).</summary>
extern float SensorModifier[8];
