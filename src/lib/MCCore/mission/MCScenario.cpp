#include "stdafx.h"
#include "mission/MCScenario.h"
#include "engine/MCCraterManager.h"
#include "object/MCEffectSystem.h"
#include "appear/MCAppearanceTypeList.h"
#include "engine/MCElementBuffer.h"
#include "lib/MCFatal.h"
#include "logistics/MCBriefingScreen.h"
#include "sprite/MCSpriteManager.h"
#include "abl/MCAblRuntime.h"
#include "ai/MCMoveSystem.h"
#include "camera/MCCameraList.h"
#include "gui/MCGuiSystem.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "object/MCCollisionSystem.h"
#include "object/MCContactSystem.h"
#include "object/MCForces.h"
#include "object/MCGameObject.h"
#include "object/MCMechWarrior.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectType.h"
#include "object/MCTrainManager.h"
#include "platform/MCFrameLog.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTerrain.h"

float ActualTime = 0.0f;
int NextStep = 0;
int PrevStep = 0;
int32_t ScenarioEndTurn = -1;
std::array<std::weak_ptr<MCMover>, MoverRosterSize> MoverRoster = {};
int32_t MineLayThrottle = 0;
int32_t MineSweepThrottle = 0;
float MineWaitTime = 0.0f;
std::array<int32_t, VisualRangeTableSize> VisualRangeTable = {};
bool DrawRevealedTacMap = false;
uint8_t ForceAlways = 0;

namespace
{
    /// <summary>The GUI timer id of objective 0's timer (the rest follow it).</summary>
    constexpr int16_t ObjectiveTimerId = 0x49f1;
    /// <summary>The event the objective timers send (the ABL scripts see it expire).</summary>
    constexpr int32_t ObjectiveTimerEvent = 0x1405;
    /// <summary>Betty's two-minute warning.</summary>
    constexpr int32_t TwoMinuteWarningSample = 8;
    /// <summary>Betty's thirty-second warning.</summary>
    constexpr int32_t ThirtySecondWarningSample = 7;
    /// <summary>The string resource naming the tonnage bonus (a printf format taking the tons).</summary>
    constexpr uint32_t TonnageBonusNameString = 0x376;
}

auto Scenario() -> std::shared_ptr<MCScenario>
{
    return MCGameContext::Current().Scenario();
}

auto WaypointMarkerShapes() -> std::span<uint8_t>
{
    std::shared_ptr<const MCScenario> scenario = Scenario();
    return scenario != nullptr ? scenario->WaypointMarkers() : std::span<uint8_t>();
}

MCScenario::MCScenario() = default;

MCScenario::~MCScenario() = default;

auto MCScenario::Update() -> int32_t
{
    if (FrameLength <= 0.0f)
    {
        FrameLength = DefaultFrameLength;
    }

    if (MaxFrameLength < FrameLength)
    {
        FrameLength = MaxFrameLength;
    }

    ScenarioTime = ScenarioTime + FrameLength;

    if (MissionStartTime == 0)
    {
        if (MultiPlayer() != nullptr && 10.0f < ScenarioTime)
        {
            Fatal(0, " runningTime is not working...why? ");
        }
    }
    else
    {
        RunningTime = static_cast<float>(static_cast<double>(MCPort::Milliseconds() - MissionStartTime) * 0.001);
    }

    ActualTime = (MultiPlayer() != nullptr) ? RunningTime : ScenarioTime;

    if (0 < TimeLimit)
    {
        if (!TwoMinuteWarningPlayed && static_cast<float>(TimeLimit) - ActualTime < 120.0f)
        {
            SoundSystem()->PlayBettySample(TwoMinuteWarningSample);
            TwoMinuteWarningPlayed = true;
        }

        if (!ThirtySecondWarningPlayed && static_cast<float>(TimeLimit) - ActualTime < 30.0f)
        {
            SoundSystem()->PlayBettySample(ThirtySecondWarningSample);
            ThirtySecondWarningPlayed = true;
        }
    }

    const bool musicPending = MusicPending;
    Turn++;
    NextStep = 0;
    PrevStep = 0;

    if (Turn < StartUpTurns)
    {
        StartingUp = true;
        StartUpCountdown = (StartUpTurns - Turn) * 10;
        return 0;
    }

    StartingUp = false;
    StartUpCountdown = 0;

    if (musicPending)
    {
        if (SoundSystem() != nullptr)
        {
            SoundSystem()->StopStaticNoise();
            SoundSystem()->PlayDigitalMusic(ScenarioTuneNum, false);
        }

        GuiSystem().SetCursorVisible(true);
        MusicPending = false;
        MissionStartTime = MCPort::Milliseconds();
    }

    return 0;
}

auto MCScenario::Render(MCGuiObject& window) -> int32_t
{
    if (1 < Turn)
    {
        CameraList()->RenderView(window);
    }

    return 0;
}

auto MCScenario::Run() -> int32_t
{
    if (MultiPlayer() != nullptr && MultiPlayer()->InMission == 0)
    {
        MultiPlayer()->ProcessReceiveList();
        // Port-only (Redux HD): a session lost meanwhile ends the wait for the host's start.
        TakeHeldSessionLoss();
        return 0;
    }

    if (GamePaused != 0)
    {
        CameraList()->Update();
        return static_cast<int32_t>(ScenarioResult);
    }

    // Port-only: the frame log's parts of the game's logic (MCFrameLog), each its own block.
    {
        MCFrameLog::Scope part("logic.update");
        Update();
    }

    {
        MCFrameLog::Scope part("logic.camera");
        CameraList()->Update();
    }

    {
        MCFrameLog::Scope part("logic.terrain");
        Terrain()->Update();
    }

    {
        MCFrameLog::Scope part("logic.paths");
        PathManager().Update();

        if (MCGameContext::Current().HasTrainManager())
        {
            TrainManager().UpdateTrains();
        }
    }

    {
        MCFrameLog::Scope part("logic.objects");
        ObjectList()->Update();
    }

    {
        MCFrameLog::Scope part("logic.sensors");
        ClanTeam()->UpdateSensors();

        if (AlliedTeam() != nullptr)
        {
            AlliedTeam()->UpdateSensors();
        }

        InnerSphereTeam()->UpdateSensors();
        PotentialContactManager().UpdateStatus();
    }

    {
        MCFrameLog::Scope part("logic.collisions");
        CollisionSystem().CheckObjects();
    }

    if (Turn < 2)
    {
        StartObjectiveTimers();
    }

    {
        MCFrameLog::Scope part("logic.script");

        if (MultiPlayer() == nullptr)
        {
            ScenarioBrain->Execute(std::span(&ScenarioBrainParams, 1));
            ScenarioResult = static_cast<uint32_t>(ScenarioBrain->ReturnValue());
        }
        else
        {
            MCAblRuntime& abl = *AblRuntime();
            abl.MissionMessageCode = 0;
            abl.MissionMessageParam = 0;
            ScenarioBrain->Execute(std::span(&ScenarioBrainParams, 1));
            abl.MissionMessageCode = 0;
            abl.MissionMessageParam = 0;

            if (MultiPlayer()->IsServer == 0)
            {
                ScenarioResult = static_cast<uint32_t>(MultiPlayer()->ScenarioResult);
            }
            else
            {
                ScenarioResult = static_cast<uint32_t>(ScenarioBrain->ReturnValue());

                if (ScenarioResult != 0)
                {
                    MultiPlayer()->SendEndScenario(static_cast<int32_t>(ScenarioResult));
                }
            }
        }
    }

    {
        MCFrameLog::Scope part("logic.network");

        if (MultiPlayer() != nullptr)
        {
            if (MultiPlayer()->IsServer != 0)
            {
                MultiPlayer()->UpdateClients();
            }

            MultiPlayer()->ProcessReceiveList();
            // Port-only (Redux HD): a session lost in the start-up turns, taken once they've ended.
            TakeHeldSessionLoss();
        }
    }

    return static_cast<int32_t>(ScenarioResult);
}

auto MCScenario::Unload() -> void
{
    // Faithful: the id wraps to a short when the timer is removed (0x249f1 -> 0x49f1).
    for (uint32_t id = 0x249f1; id < static_cast<uint32_t>(Objectives.Count()) + 0x249f1u; id++)
    {
        GuiSystem().RemoveTimer(GuiSystem(), static_cast<int16_t>(id));
    }

    Assert(MCGameContext::Current().HasCollisionSystem(), 0, " collisionSystem already NULL ");
    MCGameContext::Current().SetCollisionSystem(nullptr);

    Assert(MCGameContext::Current().HasElementList(), 0, " ElementList already NULL ");
    MCGameContext::Current().SetElementList(nullptr);
    MCGameContext::Current().SetCraterManager(nullptr);

    Assert(Terrain() != nullptr, 0, " land already NULL ");
    // The terrain stays the context's while it is taken down: the objects it frees still reach for it.
    Terrain()->Unload();
    MCGameContext::Current().SetTerrain(nullptr);

    Assert(ScenarioObjectList != nullptr, 0, " scenarioObjectList already NULL ");
    ScenarioObjectList.reset();
    // The objects, their watchers and types; then the sensors and contacts.
    MCObjectSystem::Stop();
    MCGameContext::Current().SetContactSystem(nullptr);
    MCGameContext::Current().SetTrainManager(nullptr);

    Parts.clear();
    Objectives = {};
    CreatedParts.clear();

    // The commanders, then the teams.
    MCGameContext::Current().SetForces(nullptr);

    for (MCRegisteredBlock& shape : _SensorContactShapes)
    {
        shape = {};
    }

    MCGameContext::Current().SetEffectSystem(nullptr);

    Assert(MCGameContext::Current().HasAppearanceTypeList(), 0, " appearanceTypeList already NULL ");
    MCGameContext::Current().SetAppearanceTypeList(nullptr);
    Assert(SpriteManager() != nullptr, 0, " spriteManager already NULL ");
    MCGameContext::Current().SetSpriteManager(nullptr);
    Assert(CameraList() != nullptr, 0, " cameraList already NULL ");
    // The eye goes with the camera list.
    MCGameContext::Current().SetCameraList(nullptr);

    if (OldPalette != nullptr)
    {
        MCGameContext::Current().SetPalette(std::move(OldPalette));
    }

    MCGameContext::Current().SetMoveSystem(nullptr);
    _Warriors.clear();
    ScenarioBrain.reset();
    AblClose();
    _WaypointMarkers = {};
    _Loaded = false;
}

auto MCScenario::Warrior(uint32_t number) const -> std::shared_ptr<MCMechWarrior>
{
    return number < _Warriors.size() ? _Warriors[number] : nullptr;
}

auto MCScenario::CreateScenarioObject(int32_t partId) -> void
{
    std::shared_ptr<MCBaseObject> object =
        ScenarioObjectList->FindIf([partId](MCBaseObject& candidate) { return candidate.PartId == partId; });

    if (object == nullptr)
    {
        return;
    }

    if (partId < 0x200 || 0xfff < partId)
    {
        Fatal(0, " Unknown Object in createScenarioObject ");
    }

    // Take it out of the scenario list.
    std::shared_ptr<MCBaseObject> taken;

    for (const std::shared_ptr<MCObjectList>& node : ScenarioObjectList->Lists())
    {
        taken = node->Release(*object);

        if (taken != nullptr)
        {
            break;
        }
    }

    const auto gameObject = std::static_pointer_cast<MCGameObject>(object);
    std::shared_ptr<MCObjectList> list = (gameObject->GetAlignment() != -1) ? InnerSphereMechList() : ClanMechList();

    if (list != nullptr)
    {
        list->Add(std::move(taken));
    }

    gameObject->SetPotentialContact(object->ObjectClass == MCObjectClass::Elemental ? 2 : 1);
    GameObjectMap().AddObject(*gameObject);
    gameObject->SetExists(1);

    if (const auto created = std::ranges::find(CreatedParts, object->PartId, &MCCreatedPart::PartId);
        created != CreatedParts.end())
    {
        created->Created = true;
    }
}

auto MCScenario::DestroyPartObject(int32_t partNumber) -> void
{
    MCPart& part = Parts[static_cast<size_t>(partNumber)];
    const auto object = std::static_pointer_cast<MCGameObject>(LockObject(part.Object));

    if (object == nullptr)
    {
        return;
    }

    object->GetObjectType()->HandleDestruction(*object, 0);
    ObjectList()->Remove(*object);
    part.Destroyed = true;
    part.Active = 0;
    part.Exists = 0;
}

auto MCScenario::StartObjectiveTimers() -> void
{
    for (int32_t i = 0; i < Objectives.Count(); i++)
    {
        if (0.0f < Objectives[i].TimeLeft)
        {
            SetObjectiveTimer(i, Objectives[i].TimeLeft * 1000.0f);
        }
    }
}

auto MCScenario::SetObjectiveTimer(int32_t objectiveNumber, float time) const -> int32_t
{
    if (!Objectives.IsValid(objectiveNumber))
    {
        return MCObjectiveList::BadObjective;
    }

    const auto id = static_cast<int16_t>(objectiveNumber + ObjectiveTimerId);
    GuiSystem().RemoveTimer(GuiSystem(), id);
    GuiSystem().AddTimer(GuiSystem().weak_from_this(), id, static_cast<int32_t>(time), ObjectiveTimerEvent, 0, true);
    return 0;
}

auto MCScenario::CheckObjectiveTimer(int32_t objectiveNumber) const -> float
{
    if (!Objectives.IsValid(objectiveNumber))
    {
        return 0.0f;
    }

    uint32_t remaining = 0;

    if (const std::shared_ptr<MCGuiTimer> timer =
            GuiSystem().TimerManager->GetTimer(GuiSystem(), static_cast<int16_t>(objectiveNumber + ObjectiveTimerId)))
    {
        // The timer counts in scenario milliseconds.
        const uint32_t fireTime = timer->Interval + timer->LastTime;
        remaining = static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<double>(fireTime) - static_cast<double>(ScenarioTime) * 1000.0));
    }

    return static_cast<float>(static_cast<double>(remaining) * 0.001);
}

auto MCScenario::CalcResourcePointsEarned() const -> int32_t
{
    return Objectives.ResourcePointsEarned(ScenarioResult, Mission()->EndScenarioRequested != 0);
}

auto MCScenario::SetupBonus() -> void
{
    Objectives.AddTonnageBonus(MaxDeployTonnage - CurDeployTonnage, BonusTonnageDivisor, BonusPointsPerTon,
                               LoadGameString(TonnageBonusNameString, 0xfe));
}

auto MCScenario::HandleMultiplayMessage(int32_t code, int32_t param) const -> void
{
    if (ScenarioBrainHandleMessage == MCAblSymbolId::None)
    {
        return;
    }

    MCAblRuntime& abl = *AblRuntime();
    abl.MissionMessageCode = code;
    abl.MissionMessageParam = param;
    ScenarioBrain->Execute({}, ScenarioBrainHandleMessage);
    abl.MissionMessageCode = 0;
    abl.MissionMessageParam = 0;
}

auto MCScenario::CheckAnyoneInCombat() const -> void
{
    for (uint32_t i = 1; i <= NumWarriors(); i++)
    {
        std::shared_ptr<MCMechWarrior> warrior = Warrior(i);

        if (warrior == nullptr || warrior->Status != 0)
        {
            continue;
        }

        std::shared_ptr<MCGameObject> target = warrior->GetLastTarget();

        if (target != nullptr && target->GetAlignment() != warrior->Alignment && target->GetAlignment() != 0 &&
            target->IsDisabled() == 0)
        {
            InCombat = 1;
            return;
        }
    }

    InCombat = 0;
}

std::span<uint8_t> SensorBlipShape(float tonnage)
{
    if (50.0f < tonnage)
    {
        return Scenario()->SensorContactShape(0);
    }

    if (35.0f < tonnage)
    {
        return Scenario()->SensorContactShape(2);
    }

    return Scenario()->SensorContactShape(4);
}
