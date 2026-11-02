#include "stdafx.h"
#include "object/MCSensorSystem.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBigGameObject.h"
#include "object/MCContactSystem.h"
#include "object/MCForces.h"
#include "object/MCMasterComponent.h"
#include "object/MCTeam.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMechWarrior.h"
#include "sound/MCRadio.h"

int32_t MCSensorSystem::NumSensorsMade = 0;
int SensorAutomaticSuccess = 0;
char SensorSkillMoveRange[4] = {45, 59, 69, 80};
float SensorSkillMoveFactor[4][2] = {{0.8f, 0.5f}, {0.85f, 0.55f}, {0.9f, 0.6f}, {0.95f, 0.65f}};
float SensorModifier[8] = {40.0f, 0.5f, 1.0f, 100.0f, -50.0f, 0.0f, -30.0f, -40.0f};

namespace
{
    /// <summary>The skilled range before it is rounded to a float (OnSensors compares it unrounded).</summary>
    double SkilledRangeUnrounded(MCSensorSystem& sensor)
    {
        if (!IsMoverClass(LockObject(sensor.Owner)->ObjectClass))
        {
            return static_cast<double>(sensor.Multiplier) * sensor.Range;
        }

        const int32_t speedState = std::static_pointer_cast<MCMover>(LockObject(sensor.Owner))->GetSpeedState();
        const int32_t now = Turn;
        const double newRange = static_cast<double>(sensor.SpeedRange[speedState]) * sensor.Multiplier;

        if (newRange != sensor.CurrentRange && sensor.RangeChangeTurn == -1)
        {
            sensor.RangeChangeTurn = Turn + 6;
        }

        if (sensor.RangeChangeTurn == -1)
        {
            return newRange;
        }

        if (sensor.RangeChangeTurn <= now)
        {
            sensor.CurrentRange = static_cast<float>(newRange);
            sensor.RangeChangeTurn = -1;
            return newRange;
        }

        return sensor.CurrentRange -
               (sensor.CurrentRange - newRange) * (1.0 / static_cast<double>(sensor.RangeChangeTurn - now));
    }
}

//---------------------------------------------------------------------------
// SensorSystem
//---------------------------------------------------------------------------

MCSensorSystem::MCSensorSystem(int32_t id)
    : Id(id), NextScanTime(static_cast<float>(NumSensorsMade * 0.1 + 0.25)), ScanFrequency(ContactUpdateFrequency)
{
    NumSensorsMade++;
}

MCSensorSystem::~MCSensorSystem()
{
    // Issue #45: as in the original, or each mission played put off the next one's radar 6.5 s more.
    NumSensorsMade--;
}

auto MCSensorSystem::SetRange(float newRange) -> void
{
    Range = newRange;
    SpeedRange[2] = newRange;
    SpeedRange[1] = newRange;
    SpeedRange[0] = newRange;
    CurrentRange = newRange;
    RangeChangeTurn = -1;

    if (LockObject(Owner) == nullptr || !IsMoverClass(LockObject(Owner)->ObjectClass) ||
        LockObject(Owner)->GetPilot() == nullptr)
    {
        return;
    }

    // The pilot's sensor skill picks the row: the lower the skill, the more moving and running cut the range.
    float moveFactor = 1.0f;
    float runFactor = 1.0f;
    std::shared_ptr<const MCMechWarrior> pilot = LockObject(Owner)->GetPilot();

    for (int32_t row = 0; row < 4; row++)
    {
        if (static_cast<float>(pilot->Skills[2]) <= static_cast<float>(SensorSkillMoveRange[row]))
        {
            moveFactor = SensorSkillMoveFactor[row][0];
            runFactor = SensorSkillMoveFactor[row][1];
            break;
        }
    }

    SpeedRange[0] = newRange;
    SpeedRange[1] = moveFactor * newRange;
    SpeedRange[2] = runFactor * newRange;
}

auto MCSensorSystem::GetSkilledRange() -> float
{
    // Ease toward the new range over the turns left.
    return static_cast<float>(SkilledRangeUnrounded(*this));
}

auto MCSensorSystem::SetTeam(std::shared_ptr<MCTeam> newTeam) -> void
{
    ClearSensorContacts();

    if (std::shared_ptr<MCTeam> oldTeam = Team.lock())
    {
        oldTeam->RemoveSensor(*this);
        TeamSensorSlot = -1;
        TeamIndex = -1;
    }

    Team = newTeam;

    if (newTeam == nullptr)
    {
        return;
    }

    newTeam->AddSensor(shared_from_this());

    if (Team.lock() == InnerSphereTeam())
    {
        TeamIndex = 0;
    }
    else if (Team.lock() == ClanTeam())
    {
        TeamIndex = 1;
    }
    else if (Team.lock() == AlliedTeam())
    {
        TeamIndex = 2;
    }
    else
    {
        Fatal(0, " SensorSystem: Bad Team ");
    }
}

auto MCSensorSystem::Enabled() const -> int
{
    if (TeamSensorSlot < 0 || LockObject(Owner)->GetExistsAndAwake() == 0)
    {
        return 0;
    }

    if (!IsMoverClass(LockObject(Owner)->ObjectClass))
    {
        return 1;
    }

    const auto mover = std::static_pointer_cast<const MCMover>(LockObject(Owner));

    if (mover->Status != 5 && mover->Sensor != 0xff && mover->Inventory[mover->Sensor].Disabled == 0)
    {
        return 1;
    }

    return 0;
}

auto MCSensorSystem::Disable() -> void
{
    ClearSensorContacts();
    SetTeam(nullptr);
}

auto MCSensorSystem::CalcTeamEffect(MCTeam& team) const -> float
{
    const float jammerEffect = team.GetJammerEffect();
    const float ecmEffect = team.GetEcmEffect(LockObject(Owner)->GetPosition());
    return jammerEffect < ecmEffect ? jammerEffect : ecmEffect;
}

auto MCSensorSystem::CalcTeamMultipliers() -> void
{
    if (ScenarioTime == LastMultiplierTime)
    {
        return;
    }

    LastMultiplierTime = ScenarioTime;
    TeamMultiplier = {1.0f, 1.0f, 1.0f};
    const int32_t alignment = LockObject(Owner)->GetAlignment();

    if (alignment == -1)
    {
        // Unaligned: the Inner Sphere and allied effects both count, the stronger one for each slot it fills.
        float innerSphereEffect = 1.0f;

        if (InnerSphereTeam() != nullptr)
        {
            innerSphereEffect = CalcTeamEffect(*InnerSphereTeam());
        }

        float effect = 1.0f;

        if (AlliedTeam() != nullptr)
        {
            effect = CalcTeamEffect(*AlliedTeam());
        }

        if (innerSphereEffect < effect)
        {
            effect = innerSphereEffect;
        }

        TeamMultiplier[0] = effect;
        TeamMultiplier[2] = effect;
    }
    else if (alignment == 0)
    {
        if (InnerSphereTeam() != nullptr)
        {
            TeamMultiplier[0] = CalcTeamEffect(*InnerSphereTeam());
        }

        if (ClanTeam() != nullptr)
        {
            TeamMultiplier[1] = CalcTeamEffect(*ClanTeam());
        }

        if (AlliedTeam() != nullptr)
        {
            TeamMultiplier[2] = CalcTeamEffect(*AlliedTeam());
        }
    }
    else if (alignment == 1 && ClanTeam() != nullptr)
    {
        TeamMultiplier[1] = CalcTeamEffect(*ClanTeam());
    }

    Multiplier = Team.lock() != ClanTeam() ? TeamMultiplier[1] : TeamMultiplier[0];
}

auto MCSensorSystem::AddSensorContact(MCPotentialContact& contact) -> void
{
    // Port fix (OB-145): the original held at most 200 contacts, silently missing the rest.
    if (contact.SensorSlot(Id) != -1)
    {
        return;
    }

    contact.SetSensorSlot(Id, NumContacts());
    contact.NumSensors[TeamIndex]++;
    _Contacts.push_back(contact.Id);
}

auto MCSensorSystem::RemoveSensorContact(int32_t index) -> void
{
    MCPotentialContactManager& manager = PotentialContactManager();
    MCPotentialContact& contact = manager.Contact(_Contacts[index]);
    contact.SetSensorSlot(Id, -1);
    contact.NumSensors[TeamIndex]--;
    const int32_t last = NumContacts() - 1;

    if (last > 0 && index != last)
    {
        _Contacts[index] = _Contacts[last];
        manager.Contact(_Contacts[index]).SetSensorSlot(Id, index);
    }

    _Contacts.pop_back();
}

auto MCSensorSystem::RemoveSensorContact(MCPotentialContact& contact) -> void
{
    if (contact.SensorSlot(Id) != -1)
    {
        RemoveSensorContact(contact.SensorSlot(Id));
    }
}

auto MCSensorSystem::ClearSensorContacts() -> void
{
    while (!_Contacts.empty())
    {
        RemoveSensorContact(0);
    }
}

auto MCSensorSystem::UpdateContacts() -> void
{
    if (TeamSensorSlot == -1 || Range == -1.0f)
    {
        return;
    }

    if (LockObject(Owner)->GetAwake() == 0 || LockObject(Owner)->GetExists() == 0 || Enabled() == 0)
    {
        ClearSensorContacts();
        return;
    }

    if (ScenarioTime == LastScanTime)
    {
        return;
    }

    CalcTeamMultipliers();

    if (_Contacts.empty())
    {
        return;
    }

    int32_t index = 0;

    do
    {
        const std::shared_ptr<MCBigGameObject> target =
            LockObject(PotentialContactManager().Contact(_Contacts[index]).Object);

        if (target->IsDisabled() == 0 && OnSensors(*target) != 0)
        {
            index++;
        }
        else
        {
            RemoveSensorContact(index);
        }
    } while (index < NumContacts());
}

auto MCSensorSystem::UpdateScan(int forceScan) -> void
{
    if (forceScan == 0 && (TeamSensorSlot == -1 || Range == -1.0f || Turn <= 1))
    {
        return;
    }

    if (LockObject(Owner)->GetAwake() == 0 || LockObject(Owner)->GetExists() == 0 || Enabled() == 0)
    {
        ClearSensorContacts();
        return;
    }

    if (ScenarioTime <= NextScanTime && forceScan == 0)
    {
        return;
    }

    LockObject(Owner)->GetAlignment();
    CalcTeamMultipliers();

    if (ScanBattlefield() > 0 && IsMoverClass(LockObject(Owner)->ObjectClass))
    {
        LockObject(Owner)->GetPilot()->RadioMessage(MCRadioMessageType::SensorContact, 0);
    }

    LastScanTime = ScenarioTime;

    if (forceScan != 0)
    {
        return;
    }

    NextScanTime = ScanFrequency + NextScanTime;
}

auto MCSensorSystem::ScanBattlefield(MCPotentialContactType type) -> int32_t
{
    if (Team.lock() == nullptr)
    {
        Fatal(0, " Sensor Owner has no team ");
    }

    if (TeamSensorSlot == -1 || Range == -1.0f)
    {
        return 0;
    }

    int32_t newContacts = 0;
    std::shared_ptr<const MCTeam> ownerTeam = LockObject(Owner)->GetTeam();

    for (const uint16_t contactId : PotentialContactManager().List(type))
    {
        MCPotentialContact& contact = PotentialContactManager().Contact(contactId);
        const std::shared_ptr<MCBigGameObject> target = LockObject(contact.Object);

        if (target->GetTeam() == ownerTeam)
        {
            continue;
        }

        if (OnSensors(*target) == 0)
        {
            RemoveSensorContact(contact);
        }
        else
        {
            if (contact.ContactStatus[TeamIndex] == MCContactStatus::None)
            {
                newContacts++;
            }

            AddSensorContact(contact);
        }
    }

    TotalContacts += newContacts;
    return newContacts;
}

auto MCSensorSystem::ScanBattlefield() -> int32_t
{
    switch (LockObject(Owner)->GetAlignment())
    {
        case -1:
        {
            const int32_t innerSphere = ScanBattlefield(MCPotentialContactType::InnerSphere);
            const int32_t allied = ScanBattlefield(MCPotentialContactType::Allied);
            return innerSphere + allied;
        }
        case 0:
        {
            const int32_t innerSphere = ScanBattlefield(MCPotentialContactType::InnerSphere);
            const int32_t clan = ScanBattlefield(MCPotentialContactType::Clan);
            const int32_t allied = ScanBattlefield(MCPotentialContactType::Allied);
            return innerSphere + clan + allied;
        }
        case 1:
        {
            // The Inner Sphere list is scanned, but its new contacts aren't counted.
            ScanBattlefield(MCPotentialContactType::InnerSphere);
            const int32_t clan = ScanBattlefield(MCPotentialContactType::Clan);
            const int32_t allied = ScanBattlefield(MCPotentialContactType::Allied);
            return clan + allied;
        }
        default:
            return 0;
    }
}

auto MCSensorSystem::OnSensors(MCGameObject& target) -> int
{
    if (TeamSensorSlot == -1)
    {
        return 0;
    }

    if (Range <= 0.0f)
    {
        return 0;
    }

    if (SensorAutomaticSuccess != 0)
    {
        return 1;
    }

    std::shared_ptr<const MCPotentialContact> contact = target.GetPotentialContact();

    if (contact != nullptr)
    {
        if (contact->Visibility == 2)
        {
            return 0;
        }

        if (contact->Visibility == 3 && Team.lock()->LineOfSight(target.GetPosition()) != 0)
        {
            return 0;
        }
    }

    float distance;
    double skilledRange;

    if (IsMoverClass(LockObject(Owner)->ObjectClass))
    {
        const auto mover = std::static_pointer_cast<const MCMover>(LockObject(Owner));

        if (mover->Sensor == 0xff)
        {
            return 0;
        }

        if (mover->Inventory[mover->Sensor].Disabled != 0)
        {
            return 0;
        }

        MCVector3D targetPosition = target.GetPosition();
        distance = static_cast<float>(LockObject(Owner)->DistanceFrom(targetPosition));
        skilledRange = SkilledRangeUnrounded(*this);
        // A working probe reaches hidden targets within its share of the range.
        double probeRange = -1.0;

        if (mover->Probe != 0xff && mover->Inventory[mover->Probe].Disabled == 0)
        {
            probeRange = static_cast<double>(MasterComponentList[mover->Inventory[mover->Probe].MasterID].RangeOrHeat) *
                         skilledRange;
        }

        if (distance <= probeRange)
        {
            return skilledRange < distance ? 0 : 1;
        }
    }
    else
    {
        MCVector3D targetPosition = target.GetPosition();
        distance = static_cast<float>(LockObject(Owner)->DistanceFrom(targetPosition));
        skilledRange = SkilledRangeUnrounded(*this);
    }

    if (target.Status == 5)
    {
        return 0;
    }

    return skilledRange < distance ? 0 : 1;
}

//---------------------------------------------------------------------------
// SensorSystemManager
//---------------------------------------------------------------------------

MCSensorSystemManager::MCSensorSystemManager()
{
    for (int32_t i = 0; i < InitialSensors; i++)
    {
        MakeSensor();
    }

    // The free list starts with sensor 0 and goes up.
    for (auto sensor = _Sensors.rbegin(); sensor != _Sensors.rend(); ++sensor)
    {
        _Free.push_back((*sensor)->Id);
    }
}

auto MCSensorSystemManager::MakeSensor() -> std::shared_ptr<MCSensorSystem>
{
    const auto id = static_cast<int32_t>(_Sensors.size());
    return _Sensors.emplace_back(std::make_shared<MCSensorSystem>(id));
}

auto MCSensorSystemManager::NewSensor() -> std::shared_ptr<MCSensorSystem>
{
    // The original's pool held 65 sensors and was fatal past them.
    if (_Free.empty())
    {
        return MakeSensor();
    }

    std::shared_ptr<MCSensorSystem> sensor = _Sensors[_Free.back()];
    _Free.pop_back();
    return sensor;
}

auto MCSensorSystemManager::FreeSensor(MCSensorSystem& sensor) -> void
{
    _Free.push_back(sensor.Id);
}
