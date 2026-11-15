#include "stdafx.h"
#include "object/MCMechWarrior.h"
#include "abl/MCAblDebugger.h"
#include "abl/MCAblRuntime.h"
#include "lib/MCDice.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCForces.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMoverGroup.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCMechActor.h"
#include "terrain/MCTerrain.h"

const std::array<std::string_view, NumSkills> SkillsTable = {"Piloting", "Jumping", "Sensors", "Gunnery"};
int32_t MCMechWarrior::NumWarriors = 0;
int32_t LastMoveCalcErr = 0;

MCMechWarrior::MCMechWarrior()
{
    // Spread the warriors' updates over the first frames.
    BrainUpdateTime = static_cast<float>(static_cast<double>(NumWarriors % 30) * 0.2);
    CombatUpdateTime = static_cast<float>(static_cast<double>(NumWarriors % 15) * 0.1);
    MovementUpdateTime = static_cast<float>(static_cast<double>(NumWarriors % 15) * 0.2);

    for (MCTacticalOrder& order : TacOrder)
    {
        order.Reset();
    }

    CurTacOrder.Reset();
    LastTacOrder.Reset();
    AttackOrders.Reset();
    AttackRadius = DefaultAttackRadius;
    NumWarriors++;
}

MCMechWarrior::~MCMechWarrior()
{
    NumWarriors--;
}

auto MCMechWarrior::Lobotomy() -> void
{
    Brain.reset();
    BrainAlarmHandled.fill(false);
}

namespace
{
    /// <summary>
    /// Reads a skill of the current block into <paramref name="skill"/>; <paramref name="fallback"/> when it is
    /// missing or bad.
    /// </summary>
    void ReadSkill(MCFitIniFile& file, std::string_view name, int8_t& skill, int8_t fallback)
    {
        skill = static_cast<int8_t>(file.Read<char>(name).value_or(static_cast<char>(fallback)));
    }
}

auto MCMechWarrior::Load(MCFitIniFile& warriorFile) -> int32_t
{
    if (const int32_t result = warriorFile.SeekBlock("General"); result != 0)
    {
        return result;
    }

    const MCFitResult<std::string> name = warriorFile.Read<std::string>("Name");

    if (!name.has_value())
    {
        return std::to_underlying(name.error());
    }

    DescIndex = warriorFile.Read<int32_t>("DescIndex").value_or(-1);
    NameIndex = warriorFile.Read<int32_t>("NameIndex").value_or(-1);
    Name = *name;
    NotMineYet = warriorFile.Read<bool>("NotMineYet").value_or(false) ? 1 : 0;
    Picture = warriorFile.Read<std::string>("Picture").value_or("pilotx.gif");
    const MCFitResult<std::string> callsign = warriorFile.Read<std::string>("Callsign");

    if (!callsign.has_value())
    {
        return std::to_underlying(callsign.error());
    }

    Callsign = *callsign;
    OldPilot = warriorFile.Read<uint8_t>("OldPilot").value_or(0);
    Radio.reset();

    if (const MCFitResult<std::string> audio = warriorFile.Read<std::string>("pilotAudio"); audio.has_value())
    {
        AudioStr = *audio;
        VideoStr = warriorFile.Read<std::string>("pilotVideo").value_or("");

        // A radio that fails to open goes; the sound system owns the others (until it purges them).
        if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
        {
            if (auto radio = MCRadio::Create(*sound, AudioStr, VideoStr); radio.has_value())
            {
                Radio = sound->AddRadio(std::move(*radio));
            }
        }
    }

    PaintScheme = warriorFile.Read<int32_t>("PaintScheme").value_or(-1);

    if (const int32_t result = warriorFile.SeekBlock("PersonalityTraits"); result != 0)
    {
        return result;
    }

    MCFitReader read(warriorFile);
    char professionalism = Professionalism;
    char decorum = Decorum;
    char aggressiveness = Aggressiveness;
    char courage = Courage;
    read.Value("Professionalism", professionalism);
    Professionalism = static_cast<int8_t>(professionalism);
    read.Value("Decorum", decorum);
    Decorum = static_cast<int8_t>(decorum);
    read.Value("Aggressiveness", aggressiveness);
    Aggressiveness = static_cast<int8_t>(aggressiveness);
    read.Value("Courage", courage);
    Courage = static_cast<int8_t>(courage);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    BaseCourage = Courage;

    if (const int32_t result = warriorFile.SeekBlock("Skills"); result != 0)
    {
        return result;
    }

    for (int32_t i = 0; i < NumSkills; i++)
    {
        char skill = Skills[i];
        read.Value(SkillsTable[i], skill);
        Skills[i] = static_cast<int8_t>(skill);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        SkillRank[i] = static_cast<float>(Skills[i]);
    }

    const bool haveOriginal = warriorFile.SeekBlock("OriginalSkills") == 0;

    for (int32_t i = 0; i < NumSkills; i++)
    {
        OriginalSkills[i] = Skills[i];

        if (haveOriginal)
        {
            ReadSkill(warriorFile, SkillsTable[i], OriginalSkills[i], Skills[i]);
        }
    }

    const bool haveLatest = warriorFile.SeekBlock("LatestSkills") == 0;

    for (int32_t i = 0; i < NumSkills; i++)
    {
        LatestSkills[i] = Skills[i];

        if (haveLatest)
        {
            ReadSkill(warriorFile, SkillsTable[i], LatestSkills[i], Skills[i]);
        }
    }

    const bool havePoints = warriorFile.SeekBlock("SkillPoints") == 0;

    for (int32_t i = 0; i < NumSkills; i++)
    {
        SkillPoints[i] = havePoints ? warriorFile.Read<float>(SkillsTable[i]).value_or(0.0f) : 0.0f;
    }

    CalcRank();

    if (const int32_t result = warriorFile.SeekBlock("Status"); result != 0)
    {
        return result;
    }

    const MCFitResult<char> wounds = warriorFile.Read<char>("Wounds");

    if (!wounds.has_value())
    {
        return std::to_underlying(wounds.error());
    }

    Wounds = static_cast<float>(*wounds);

    for (int32_t i = 0; i < NumSkills; i++)
    {
        NumSkillUses[i][0] = 0;
        NumSkillUses[i][1] = 0;
        NumSkillSuccesses[i][0] = 0;
        NumSkillSuccesses[i][1] = 0;
    }

    for (int32_t i = 0; i < 5; i++)
    {
        NumKilled[i][0] = 0;
        NumKilled[i][1] = 0;
    }

    // Whether the pilot would survive ejecting: piloting + 30 percent, at most 94.
    const int32_t roll = RandomNumber(100);
    EscapesThruEjection = (roll <= Skills[SkillPiloting] + 30 && roll < 95) ? 1 : 0;
    return 0;
}

auto MCMechWarrior::RadioMessage(int32_t messageId, int propogateIfMultiplayer) -> void
{
    // Issue #25: any id below 0 is none (a script's playspeech can give one; the original let -2 and below through).
    if (messageId >= RadioMessageTypeCount || Radio.lock() == nullptr || Status != 0 || messageId < 0 || Turn <= 0)
    {
        return;
    }

    if (UnderHomeCommand() == 0)
    {
        if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0 && propogateIfMultiplayer != 0)
        {
            std::static_pointer_cast<MCMover>(LockObject(Vehicle))->AddRadioChunk(0, static_cast<uint8_t>(messageId));
        }

        return;
    }

    switch (static_cast<MCRadioMessageType>(messageId))
    {
        case MCRadioMessageType::SensorContact:
        {
            if (static_cast<double>(ScenarioTime) - 15.0 < LastContactTime)
            {
                return;
            }

            LastContactTime = ScenarioTime;
            break;
        }
        case MCRadioMessageType::UnderAttack:
        {
            if (static_cast<double>(ScenarioTime) - 20.0 < LastUnderAttackTime)
            {
                return;
            }

            LastUnderAttackTime = ScenarioTime;
            break;
        }
        case MCRadioMessageType::Weapons50:
        {
            if (Weapons50Sent != 0)
            {
                return;
            }

            Weapons50Sent = 1;
            break;
        }
        case MCRadioMessageType::WeaponsOut:
        {
            if (WeaponsOutSent != 0)
            {
                return;
            }

            WeaponsOutSent = 1;
            break;
        }
        default:
            break;
    }

    // Some messages may repeat at once; the rest wait 10 seconds before the same one plays again.
    switch (static_cast<MCRadioMessageType>(messageId))
    {
        case MCRadioMessageType::MoveTo:
        case MCRadioMessageType::RunTo:
        case MCRadioMessageType::JumpTo:
        case MCRadioMessageType::AllStop:
        case MCRadioMessageType::Attack:
        case MCRadioMessageType::RangeAttack:
        case MCRadioMessageType::AttackFromHere:
        case MCRadioMessageType::AttackRam:
        case MCRadioMessageType::Dfa:
        case MCRadioMessageType::AttackBody:
        case MCRadioMessageType::Capture:
        case MCRadioMessageType::Refit:
        case MCRadioMessageType::Power:
        case MCRadioMessageType::MoveBlocked:
        case MCRadioMessageType::IllegalOrder:
        case MCRadioMessageType::Deploy:
        case MCRadioMessageType::Load:
            break;
        default:
        {
            if (LastMessageType == messageId && static_cast<double>(ScenarioTime) - 10.0 < LastMessageTime)
            {
                return;
            }
            break;
        }
    }

    LastMessageTime = ScenarioTime;
    const int32_t played = Radio.lock()->PlayMessage(static_cast<MCRadioMessageType>(messageId));
    LastMessageType = messageId;
    LastMessage = played;
}

auto MCMechWarrior::GetAggressiveness(int current) const -> int32_t
{
    if (current != 0 && CurTacOrder.IsCombatOrder() != 0)
    {
        return (100 - Aggressiveness) / 2 + Aggressiveness;
    }

    return Aggressiveness;
}

auto MCMechWarrior::AddQueuedTacOrder(MCTacticalOrder tacOrder) -> int32_t
{
    MCQueuedTacOrder queued;
    queued.Point = tacOrder.GetWayPoint(0);
    queued.Id = tacOrder.Id;
    queued.PackedData = {tacOrder.Data[0], tacOrder.Data[1]};

    if (!QueuedOrders.Push(queued))
    {
        return 2;
    }

    // The first order queued starts at once, unless a player order is waiting or one from the queue is running.
    if ((MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0) && QueuedOrders.Size() == 1 &&
        NewTacOrderReceivedOf(MCOrderState::Player) == 0 &&
        (PlayerOrderFromQueue == 0 || CurTacOrder.Origin != MCOrderOrigin::Player))
    {
        ExecuteTacOrderQueue();
    }

    return 0;
}

namespace
{
    /// <summary>Rebuilds a queued order into <paramref name="tacOrder"/>.</summary>
    void UnpackQueuedOrder(const MCQueuedTacOrder& queued, MCTacticalOrder& tacOrder)
    {
        tacOrder.Data[0] = queued.PackedData[0];
        tacOrder.Data[1] = queued.PackedData[1];
        tacOrder.Unpack();
        tacOrder.Id = queued.Id;
        tacOrder.SetWayPoint(0, queued.Point);
    }
}

auto MCMechWarrior::RemoveQueuedTacOrder(MCTacticalOrder& tacOrder) -> int32_t
{
    if (QueuedOrders.Empty())
    {
        return 2;
    }

    const MCQueuedTacOrder queued = QueuedOrders.Front();
    QueuedOrders.PopFront();
    UnpackQueuedOrder(queued, tacOrder);
    return 0;
}

auto MCMechWarrior::PeekQueuedTacOrder(MCTacticalOrder& tacOrder) const -> int32_t
{
    if (QueuedOrders.Empty())
    {
        return 2;
    }

    UnpackQueuedOrder(QueuedOrders.Front(), tacOrder);
    return 0;
}

auto MCMechWarrior::ClearTacOrderQueue() -> void
{
    QueuedOrders.Clear();
    TacOrderQueueExecuting = false;
}

auto MCMechWarrior::ExecuteTacOrderQueue() -> void
{
    if (!QueuedOrders.Empty())
    {
        TacOrderQueueExecuting = true;
        MCTacticalOrder order;
        order.Reset();

        if (RemoveQueuedTacOrder(order) == 0)
        {
            SetPlayerTacOrder(order, 1);
        }

        return;
    }

    TacOrderQueueExecuting = false;
}

auto MCMechWarrior::GetTacOrderQueue() -> std::vector<MCQueuedTacOrder>
{
    std::vector<MCQueuedTacOrder> list;

    if (PlayerOrderFromQueue != 0)
    {
        const MCTacticalOrder& playerOrder = TacOrderOf(MCOrderState::Player);
        MCQueuedTacOrder& current = list.emplace_back();
        current.Id = playerOrder.Id;
        current.Point = playerOrder.GetWayPoint(0);
        current.PackedData = {playerOrder.Data[0], playerOrder.Data[1]};
    }

    list.append_range(QueuedOrders.Orders());
    return list;
}

auto MCMechWarrior::GetTacOrderQueueSize() const -> int32_t
{
    return (PlayerOrderFromQueue != 0 ? 1 : 0) + QueuedOrders.Size();
}

auto MCMechWarrior::UpdateClientOrderQueue(int32_t tacOrderId) -> void
{
    MCTacticalOrder order;
    order.Reset();
    int32_t result = PeekQueuedTacOrder(order);

    if (tacOrderId == 0)
    {
        if (result == 0 && order.Id == LastTacOrderId)
        {
            RemoveQueuedTacOrder(order);
        }

        return;
    }

    LastTacOrderId = tacOrderId;

    while (result == 0 && CompareTacOrderId(order.Id, tacOrderId) < 0)
    {
        RemoveQueuedTacOrder(order);
        result = PeekQueuedTacOrder(order);
    }
}

auto MCMechWarrior::GetGroup() const -> std::shared_ptr<MCMoverGroup>
{
    if (std::shared_ptr<MCGameObject> vehicle = LockObject(Vehicle))
    {
        return static_cast<MCMover&>(*vehicle).Group.lock();
    }

    return nullptr;
}

auto MCMechWarrior::GetPoint() const -> std::shared_ptr<MCMover>
{
    if (std::shared_ptr<MCMoverGroup> group = GetGroup())
    {
        return group->GetPoint();
    }

    return nullptr;
}

auto MCMechWarrior::OnHomeTeam() const -> int
{
    return Team.lock() == HomeTeam() ? 1 : 0;
}

auto MCMechWarrior::UnderHomeCommand() const -> int
{
    if (LockObject(Vehicle) != nullptr)
    {
        return std::static_pointer_cast<MCMover>(LockObject(Vehicle))->NetPlayerId >= 0 ? 1 : 0;
    }

    return 0;
}

auto MCMechWarrior::CheckSkill(int32_t skillId, float factor) -> int32_t
{
    NumSkillUses[skillId][1]++;
    SkillPoints[skillId] = SkillTry[skillId] + SkillPoints[skillId];
    const int32_t roll = RandomNumber(100);
    const int32_t margin = static_cast<int32_t>(static_cast<double>(Skills[skillId]) * factor) - roll - 1;

    if (margin >= 0 && skillId != SkillSensors)
    {
        NumSkillSuccesses[skillId][1]++;
        SkillPoints[skillId] = SkillSuccess[skillId] + SkillPoints[skillId];
    }

    return margin;
}

auto MCMechWarrior::Injure(float numWounds, int checkEject) -> int
{
    if (Status != 0)
    {
        return 0;
    }

    if (numWounds > 0.0f)
    {
        RadioMessage(MCRadioMessageType::PilotHurt, 0);
    }

    Wounds = numWounds + Wounds;

    if (static_cast<double>(Wounds) < 6.0)
    {
        return 0;
    }

    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));
    Assert(mover != nullptr, 0, " Pilot has no vehicle ");

    if (checkEject != 0)
    {
        float points = SkillTry[SkillPiloting] + SkillPoints[SkillPiloting];
        SkillPoints[SkillPiloting] = points;
        NumSkillUses[SkillPiloting][1]++;

        if (EscapesThruEjection != 0)
        {
            points = points + SkillSuccess[SkillPiloting];
            Wounds = 5.0f;
            NumSkillSuccesses[SkillPiloting][1]++;
            SkillPoints[SkillPiloting] = points;

            if (mover->HandleEjection() == 0)
            {
                Wounds = 6.0f;
            }
        }
    }

    if (static_cast<double>(Wounds) >= 6.0)
    {
        RadioMessage(MCRadioMessageType::Death, 0);
        Status = 4;
    }

    if (mover != nullptr)
    {
        mover->Disable(2);
    }

    if (GetGroup() != nullptr)
    {
        GetGroup()->HandleMateDestroyed(static_cast<uint32_t>(mover->PartId));
    }

    if (Radio.lock() != nullptr)
    {
        Radio.lock()->Enabled = false;
    }

    return 1;
}

auto MCMechWarrior::Eject() -> void
{
    if (Status != 0 && Status != 1)
    {
        return;
    }

    if (Wounds < 6.0f)
    {
        Wounds = Wounds + 1.0f;
    }

    if (Wounds < 6.0f)
    {
        RadioMessage(MCRadioMessageType::Ejecting, 0);
        Status = 3;
    }
    else
    {
        RadioMessage(MCRadioMessageType::Death, 0);
        Status = 4;
    }

    const auto mover = std::static_pointer_cast<MCMover>(LockObject(Vehicle));

    if (mover != nullptr)
    {
        mover->Disable(3);
    }

    if (GetGroup() != nullptr)
    {
        MCMoverGroup::HandleMateEjected(static_cast<uint32_t>(mover->PartId));
    }

    if (Radio.lock() != nullptr)
    {
        Radio.lock()->Enabled = false;
    }
}

auto MCMechWarrior::SetTeam(std::shared_ptr<MCTeam> newTeam) -> void
{
    Team = newTeam;
    Alignment = static_cast<int8_t>(newTeam->Alignment);
}

auto MCMechWarrior::SetVehicle(std::shared_ptr<MCGameObject> newVehicle) -> void
{
    const MCObjectClass objectClass = newVehicle->ObjectClass;

    if (objectClass != MCObjectClass::BattleMech && objectClass != MCObjectClass::GroundVehicle &&
        objectClass != MCObjectClass::Elemental && objectClass != MCObjectClass::Mover)
    {
        Fatal(0, " bad vehicle type ");
    }

    Vehicle = newVehicle;

    if (Radio.lock() != nullptr)
    {
        Radio.lock()->Owner = weak_from_this();
    }
}

auto MCMechWarrior::GetVehicle() const -> std::shared_ptr<MCGameObject>
{
    return LockObject(Vehicle);
}

auto MCMechWarrior::SetBrain(int32_t brainHandle) -> int32_t
{
    if (Brain != nullptr)
    {
        Lobotomy();
    }

    if (brainHandle < 0)
    {
        return 0;
    }

    Brain = MCAblModule::Create(brainHandle);
    Brain->SetName(std::format("Pilot {}", Name.empty() ? std::string("(null)") : Name));

    for (size_t i = 0; i < BrainAlarmHandled.size(); i++)
    {
        BrainAlarmHandled[i] = Brain->FindFunction(PilotAlarmFunctionName[i], true) != MCAblSymbolId::None;
    }

    return 0;
}

auto MCMechWarrior::RunBrain() -> int32_t
{
    if (Brain == nullptr)
    {
        return 0;
    }

    const std::shared_ptr<MCGameObject> vehicle = LockObject(Vehicle);
    MCAblBrainScope brain(GetGroup(), vehicle, static_cast<int32_t>(vehicle->ObjectClass), weak_from_this());
    Brain->Execute();
    return Brain->ReturnValue();
}

auto MCMechWarrior::GetVehicleStatus() const -> int32_t
{
    if (LockObject(Vehicle) != nullptr)
    {
        return static_cast<uint8_t>(LockObject(Vehicle)->Status);
    }

    return -1;
}

auto MCMechWarrior::UpdateAttackerStatus(uint32_t attackerId, float time) -> void
{
    if (const auto attacker = std::ranges::find(Attackers, attackerId, &MCAttackerRec::AttackerId);
        attacker != Attackers.end())
    {
        attacker->LastTime = time;
    }
    else if (std::ssize(Attackers) < MaxAttackers)
    {
        Attackers.push_back({attackerId, time});
    }
}

auto MCMechWarrior::GetAttackerInfo(uint32_t attackerId) const -> std::optional<MCAttackerRec>
{
    const auto attacker = std::ranges::find(Attackers, attackerId, &MCAttackerRec::AttackerId);

    if (attacker == Attackers.end())
    {
        return std::nullopt;
    }

    return *attacker;
}

auto MCMechWarrior::GetAttackers(float seconds) const -> std::vector<uint32_t>
{
    const float since = ScenarioTime - seconds;
    std::vector<uint32_t> attackers;

    for (const MCAttackerRec& attacker : Attackers)
    {
        if (since <= attacker.LastTime)
        {
            attackers.push_back(attacker.AttackerId);
        }
    }

    return attackers;
}

auto MCMechWarrior::SetAttackTarget(std::shared_ptr<MCGameObject> object) -> int32_t
{
    AttackOrders.Target = object;
    AttackOrders.TargetTime = ScenarioTime;
    return 0;
}

auto MCMechWarrior::GetLastTarget() -> std::shared_ptr<MCGameObject>
{
    std::shared_ptr<MCGameObject> target = LockObject(LastTarget);

    if (target == nullptr)
    {
        return nullptr;
    }

    if (target->IsDestroyed() == 0 && (target->IsDisabled() == 0 || LastTargetObliterate != 0) &&
        (target->GetAlignment() != Alignment || LastTargetFriendly != 0))
    {
        if (LastTargetConserveAmmo != 0)
        {
            CurTacOrder.AttackParams.Type = 3;
        }

        return target;
    }

    // Dead, disabled or friendly: forget it (and an attack order on it).
    SetLastTarget(nullptr, 0, 0);
    LastTargetTime = -1.0f;
    LastTargetObliterate = 0;
    LastTargetFriendly = 0;

    if (CurTacOrder.IsCombatOrder() != 0)
    {
        ClearCurTacOrder(1, 0);
    }

    return nullptr;
}

auto MCMechWarrior::SetLastTarget(std::shared_ptr<MCGameObject> target, int obliterate, int conserveAmmo) -> void
{
    if (const std::shared_ptr<MCGameObject> vehicle = LockObject(Vehicle);
        vehicle != nullptr && static_cast<MCMover&>(*vehicle).NetPlayerId > -1)
    {
        if (const std::shared_ptr<MCGameObject> lastTarget = LockObject(LastTarget);
            lastTarget != nullptr && lastTarget->GetObjectType() != nullptr)
        {
            lastTarget->DecrementAttackers();
        }

        if (target != nullptr)
        {
            target->IncrementAttackers();
        }
    }

    const bool taken = target != nullptr && target != LockObject(LastTarget);
    LastTarget = target;

    if (target == nullptr)
    {
        LastTargetFriendly = 0;
    }
    else
    {
        LastTargetFriendly = target->GetAlignment() == Alignment ? 1 : 0;
    }

    LastTargetTime = ScenarioTime;
    LastTargetObliterate = obliterate;
    LastTargetConserveAmmo = conserveAmmo;

    // Port-only (HD art, SPEC-first-use.md: a unit's first frames, by intent): a target taken, its mech's firing pose
    // toward it read before its torso turns.
    if (const std::shared_ptr<MCGameObject> vehicle = LockObject(Vehicle); taken && vehicle != nullptr)
    {
        MCReadMechAimAhead(*vehicle, target->GetPosition());
    }
}

auto MCMechWarrior::SetCurrentTarget(std::shared_ptr<MCGameObject> target) -> void
{
    SetLastTarget(target, 0, 0);
}

auto MCMechWarrior::ClearAttackOrders() -> void
{
    AttackOrders.Origin = 1;
    AttackOrders.Type = 0;
    AttackOrders.Target.reset();
    AttackOrders.AimLocation = -1;
    AttackOrders.Pursue = 0;
    AttackOrders.TargetTime = -1.0f;
}

auto MCMechWarrior::ClearMoveOrders() -> void
{
    ClearMoveGoal();
    ClearMoveWayPath();

    for (int32_t i = 0; i < 2; i++)
    {
        if (MoveOrders.Path[i] != nullptr)
        {
            MoveOrders.Path[i]->Clear();
        }
    }

    MoveOrders.MoveState = MCMoveState::Forward;
    MoveOrders.MoveStateGoal = MCMoveState::Forward;
    MoveOrders.YieldState = 0;
    MoveOrders.MoveStateGoalChanged = 0;
    MoveOrders.YieldTime = -1.0f;
    MoveOrders.WaitForPointTime = -1.0f;
    MoveOrders.TimeOfLastStep = -1.0f;
    SetMoveGlobalPath({});
    PathManager().Remove(*this);
}

auto MCMechWarrior::DebugPrint(std::string_view text, int debugMode) -> void
{
    if (const std::shared_ptr<MCAblDebugger> debugger = AblGetDebugger())
    {
        debugger->Print(text);

        if (debugMode != 0)
        {
            debugger->DebugMode();
        }
    }
}

auto MCMechWarrior::DebugOrders() -> void
{
    const int32_t targetId = LockObject(CurTacOrder.Target) != nullptr ? LockObject(CurTacOrder.Target)->PartId : 0;
    const MCVector3D point = CurTacOrder.GetWayPoint(0);
    std::string line;

    switch (CurTacOrder.Code)
    {
        case MCTacticalOrderCode::None:
            line = "CURRENT ORDERS: None";
            break;
        case MCTacticalOrderCode::Wait:
            line = "CURRENT ORDERS: Wait";
            break;
        case MCTacticalOrderCode::MoveToPoint:
            line = std::format("CURRENT ORDERS: Move to ({:.2f}, {:.2f}, {:.2f})", point.X, point.Y, point.Z);
            break;
        case MCTacticalOrderCode::MoveToObject:
            line = std::format("CURRENT ORDERS: Move To Object {}", targetId);
            break;
        case MCTacticalOrderCode::JumpToPoint:
            line = std::format("CURRENT ORDERS: Jump to ({:.2f}, {:.2f}, {:.2f})", point.X, point.Y, point.Z);
            break;
        case MCTacticalOrderCode::JumpToObject:
            line = std::format("CURRENT ORDERS: Jump To Object {}", targetId);
            break;
        case MCTacticalOrderCode::TraversePath:
            line = "CURRENT ORDERS: Traverse Path";
            break;
        case MCTacticalOrderCode::PatrolPath:
            line = "CURRENT ORDERS: Patrol Path";
            break;
        case MCTacticalOrderCode::Escort:
            line = "CURRENT ORDERS: Escort";
            break;
        case MCTacticalOrderCode::Follow:
            line = "CURRENT ORDERS: Follow";
            break;
        case MCTacticalOrderCode::Guard:
            line = "CURRENT ORDERS: Guard";
            break;
        case MCTacticalOrderCode::Stop:
            line = "CURRENT ORDERS: Stop";
            break;
        case MCTacticalOrderCode::PowerUp:
            line = "CURRENT ORDERS: Power Up";
            break;
        case MCTacticalOrderCode::PowerDown:
            line = "CURRENT ORDERS: Power Down";
            break;
        case MCTacticalOrderCode::WayPointsDone:
            line = "CURRENT ORDERS: Formation";
            break;
        case MCTacticalOrderCode::Eject:
            line = "CURRENT ORDERS: Eject";
            break;
        case MCTacticalOrderCode::AttackObject:
            line = std::format("CURRENT ORDERS: Attack Object {}", targetId);
            break;
        case MCTacticalOrderCode::HoldFire:
            line = "CURRENT ORDERS: Hold Fire";
            break;
        case MCTacticalOrderCode::Withdraw:
            line = "CURRENT ORDERS: Withdraw";
            break;
        default:
            line = "CURRENT ORDERS: Unknown Tac Order Type";
            break;
    }

    DebugPrint(line, 0);
    std::shared_ptr<MCGameObject> target = GetLastTarget();
    DebugPrint(std::format("     CURRENT TARGET: Object {}", target != nullptr ? target->PartId : 0), 0);
}

auto MCMechWarrior::CalcRank() -> void
{
    double weightedSum = 0.0;
    double totalWeight = 0.0;

    for (int32_t i = 0; i < NumSkills; i++)
    {
        weightedSum = static_cast<double>(SkillRank[i]) * SkillWeightings[i] + weightedSum;
        totalWeight = totalWeight + SkillWeightings[i];
    }

    const float rankValue = static_cast<float>(weightedSum / totalWeight);

    for (int32_t i = 0; i < 4; i++)
    {
        if (rankValue < WarriorRankScale[i])
        {
            Rank = static_cast<uint8_t>(i);
            return;
        }
    }
}

auto MCMechWarrior::LoadBrainParameters(MCFitIniFile& brainFile, int32_t warriorId) -> int32_t
{
    if (Brain == nullptr)
    {
        Fatal(0, " Warrior.loadBrainParameters: NULL brain ");
    }

    const std::string blockName = std::format("Warrior{}", warriorId);

    if (const int32_t result = brainFile.SeekBlock(blockName); result != 0)
    {
        return result;
    }

    MCFitReader read(brainFile);
    int32_t numCells = 0;
    int32_t numStaticVars = 0;
    read.Value("NumCells", numCells);
    read.Value("NumStaticVars", numStaticVars);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    for (int32_t i = 0; i < numCells; i++)
    {
        if (const int32_t result = brainFile.SeekBlock(std::format("{}Cell{}", blockName, i)); result != 0)
        {
            return result;
        }

        int32_t cell = 0;
        int32_t memType = 0;
        read.Value("Cell", cell);
        read.Value("MemType", memType);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        if (memType == 0)
        {
            int32_t value = 0;
            read.Value("Value", value);

            if (read.Failed())
            {
                return std::to_underlying(read.Error());
            }

            Memory[cell].Integer = value;
        }
        else if (memType == 1)
        {
            float value = 0.0f;
            read.Value("Value", value);

            if (read.Failed())
            {
                return std::to_underlying(read.Error());
            }

            Memory[cell].Real = value;
        }
        else
        {
            return 0x29a;
        }
    }

    for (int32_t i = 0; i < numStaticVars; i++)
    {
        if (const int32_t result = brainFile.SeekBlock(std::format("{}Static{}", blockName, i)); result != 0)
        {
            return result;
        }

        int32_t type = 0;
        std::string varName;
        read.Value("type", type);
        read.Value("Name", varName);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        switch (type)
        {
            case 0:
            {
                int32_t value = 0;
                read.Value("Value", value);

                if (read.Failed())
                {
                    return std::to_underlying(read.Error());
                }

                Brain->SetStaticInteger(varName, value);
                break;
            }

            case 1:
            {
                float value = 0.0f;
                read.Value("Value", value);

                if (read.Failed())
                {
                    return std::to_underlying(read.Error());
                }

                Brain->SetStaticReal(varName, value);
                break;
            }

            case 2:
            {
                int32_t numValues = 0;
                read.Value("NumValues", numValues);

                if (read.Failed())
                {
                    return std::to_underlying(read.Error());
                }

                std::vector<int32_t> values(static_cast<size_t>(std::max(numValues, 0)));

                if (const MCFitResult<uint32_t> result = brainFile.ReadArray<int32_t>("Values", values);
                    !result.has_value())
                {
                    return std::to_underlying(result.error());
                }

                Brain->SetStaticIntegerArray(varName, values);
                break;
            }

            case 3:
            {
                int32_t numValues = 0;
                read.Value("NumValues", numValues);

                if (read.Failed())
                {
                    return std::to_underlying(read.Error());
                }

                std::vector<float> values(static_cast<size_t>(std::max(numValues, 0)));

                if (const MCFitResult<uint32_t> result = brainFile.ReadArray<float>("Values", values);
                    !result.has_value())
                {
                    return std::to_underlying(result.error());
                }

                Brain->SetStaticRealArray(varName, values);
                break;
            }

            default:
                return 0x29b;
        }
    }

    return 0;
}
