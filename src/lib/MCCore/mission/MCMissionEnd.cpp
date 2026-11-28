#include "stdafx.h"
#include "mission/MCMissionEnd.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "sound/MCSoundSystem.h"

uint32_t ResultsStepTicks = 20;
int EventsToMissionResultsScreen = 0;

namespace
{
    /// <summary>The count's sounds (1998's interface samples).</summary>
    constexpr uint32_t CountSound = 0x42;
    constexpr uint32_t CountedSound = 0x43;
    constexpr uint32_t LineSound = 0x47;
    constexpr uint32_t PilotSound = 0x10;
    constexpr uint32_t DebriefingSound = 0x32;
    /// <summary>Betty's lines: the tonnage bonus, a loss, a win.</summary>
    constexpr uint32_t BonusLine = 10;
    constexpr uint32_t LostLine = 0xb;
    constexpr uint32_t WonLine = 0x12;
    /// <summary>The resource points the count adds each step.</summary>
    constexpr int32_t PointsStep = 1000;
    /// <summary>The statistics listed (the last's index).</summary>
    constexpr int32_t LastStatistic = 4;
    constexpr int32_t Statistics = 5;
    /// <summary>A scenario result above this is a win (single player).</summary>
    constexpr uint32_t WonAbove = 3;
    constexpr int32_t SecondsAMinute = 60;
    /// <summary>The kinds of kill a warrior counts.</summary>
    constexpr int32_t KillKinds = 7;
    /// <summary>The room's teams, numbered from 1 (a match's HomeTeam is 0 or 1).</summary>
    constexpr int32_t Teams = 2;

    /// <summary>The room's team <paramref name="commander"/>'s pilots fought for (1 or 2); 0 when it can't be told.</summary>
    int32_t TeamOf(const MCMissionResults& results, int32_t commander, int32_t homeTeam)
    {
        if (homeTeam < 0 || homeTeam >= Teams)
        {
            return 0;
        }

        for (const MCMissionPilotResult& pilot : results.Pilots)
        {
            const std::shared_ptr<MCMechWarrior> warrior = pilot.Warrior.lock();
            const std::shared_ptr<MCGameObject> vehicle = warrior != nullptr ? LockObject(warrior->Vehicle) : nullptr;

            if (vehicle != nullptr && vehicle->GetCommanderId() == commander)
            {
                const bool ours = warrior->Alignment == HomeTeam()->Alignment;
                return ours ? homeTeam + 1 : Teams - homeTeam;
            }
        }

        return 0;
    }
}

// MCResultsCount

auto MCResultsCount::Start() -> void
{
    _Skipping = false;
    _StepIndex = 0;
    _Step = MCResultsStep::ResourcePoints;
    _NextDrawTime = MouseTicks;
    _ShownResourcePoints = -1;
}

auto MCResultsCount::Play(uint32_t sample) const -> void
{
    if (!_Skipping && SoundSystem() != nullptr)
    {
        SoundSystem()->PlayInterfaceSample(sample);
    }
}

auto MCResultsCount::Run(const MCMissionResults& results) -> void
{
    while (_NextDrawTime != 0 && (_NextDrawTime <= MouseTicks || _Skipping))
    {
        switch (_Step)
        {
            case MCResultsStep::ResourcePoints:
            {
                if (!MCMissionEnd::CountsResourcePoints())
                {
                    _Step = MCResultsStep::Statistics;
                }
                else
                {
                    StepResourcePoints(results);
                }
                break;
            }
            case MCResultsStep::Statistics:
                StepStatistics();
                break;
            case MCResultsStep::PrimaryObjectives:
            case MCResultsStep::SecondaryObjectives:
                StepObjectives();
                break;
            case MCResultsStep::Pilots:
                StepPilots(results);
                break;
            case MCResultsStep::Debriefing:
                StepDebriefing();
                break;

            default:
                break;
        }
    }
}

auto MCResultsCount::AdvanceStep() -> void
{
    _StepIndex = 0;
    _Step = static_cast<MCResultsStep>(std::to_underlying(_Step) + 1);
}

auto MCResultsCount::StepResourcePoints(const MCMissionResults& results) -> void
{
    const int32_t shown = _StepIndex * PointsStep;

    if (results.ResourcePointsEarned < shown)
    {
        _ShownResourcePoints = results.ResourcePointsEarned;
        AdvanceStep();
        _NextDrawTime = ResultsStepTicks + MouseTicks;
        Play(CountedSound);
    }
    else
    {
        _ShownResourcePoints = shown;
        _NextDrawTime = ResultsStepTicks / 20 + MouseTicks;
        Play(CountSound);
        _StepIndex++;
    }
}

auto MCResultsCount::StepStatistics() -> void
{
    _NextDrawTime = ResultsStepTicks / 2 + MouseTicks;

    if (_StepIndex < LastStatistic)
    {
        _StepIndex++;
    }
    else if (_StepIndex == LastStatistic)
    {
        AdvanceStep();
        _NextDrawTime = ResultsStepTicks + MouseTicks;
    }

    Play(LineSound);
}

auto MCResultsCount::StepObjectives() -> void
{
    // The primary step lists the type 0 objectives, the secondary step the type 1 ones.
    const uint32_t wantedType =
        _Step != MCResultsStep::PrimaryObjectives ? MCScenarioObjective::Secondary : MCScenarioObjective::Primary;
    const MCObjectiveList& objectives = Scenario()->Objectives;
    bool drew = objectives[_StepIndex].Type == wantedType;

    if (_StepIndex == objectives.Count())
    {
        // After the secondary objectives: the tonnage bonus, when it was earned.
        if (_Step == MCResultsStep::SecondaryObjectives && MCMissionEnd::ShowsTonnageBonus())
        {
            SoundSystem()->PlayBettySample(BonusLine);
            drew = true;
        }

        AdvanceStep();
    }
    else
    {
        _StepIndex++;
    }

    if (drew && MultiPlayer() == nullptr)
    {
        Play(LineSound);
        _NextDrawTime = ResultsStepTicks + MouseTicks;
    }
}

auto MCResultsCount::StepPilots(const MCMissionResults& results) -> void
{
    const int32_t index = _StepIndex;
    _NextDrawTime = ResultsStepTicks + MouseTicks;

    if (index == static_cast<int32_t>(results.Pilots.size()))
    {
        AdvanceStep();
        return;
    }

    if (results.Pilots[static_cast<size_t>(index)].Warrior.lock() != nullptr)
    {
        if (OnPilot)
        {
            OnPilot(static_cast<size_t>(index));
        }

        _StepIndex++;
    }

    Play(PilotSound);
}

auto MCResultsCount::StepDebriefing() -> void
{
    if (OnDebriefing)
    {
        OnDebriefing();
    }

    _NextDrawTime = 0;
    _Step = MCResultsStep::Done;
    Play(DebriefingSound);
}

auto MCResultsCount::StatisticsShown() const -> int32_t
{
    return _Step == MCResultsStep::Statistics ? _StepIndex : (_Step > MCResultsStep::Statistics ? Statistics : 0);
}

auto MCResultsCount::ObjectiveShown(MCResultsStep step, int32_t index) const -> bool
{
    return step < _Step || (step == _Step && index < _StepIndex);
}

auto MCResultsCount::PilotsShown(size_t lines) const -> int32_t
{
    return _Step == MCResultsStep::Pilots ? _StepIndex
                                          : (_Step > MCResultsStep::Pilots ? static_cast<int32_t>(lines) : 0);
}

// MCMissionEnd

auto MCMissionEnd::Begin() -> void
{
    EventsToMissionResultsScreen = 1;
    Mission()->StopScenarioCallbacks();
    GuiSystem().CursorHidden = false;
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
    GuiSystem().CursorHidden = true;
    GuiSystem().Release();
}

auto MCMissionEnd::Gather() -> MCMissionResults
{
    const MCScenario& scenario = *Scenario();
    return MultiPlayer() == nullptr ? GatherSinglePlayerResults(scenario, ScenarioResult)
                                    : GatherMultiplayerResults(scenario);
}

auto MCMissionEnd::CommanderLines(const MCMissionResults& results) -> std::vector<MCCommanderLine>
{
    std::vector<MCCommanderLine> lines;
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr || game->SessionManager == nullptr)
    {
        return lines;
    }

    for (int32_t i = 0; i < static_cast<int32_t>(results.Commanders.size()); i++)
    {
        const MCMissionCommanderScore& score = results.Commanders[static_cast<size_t>(i)];
        const auto player = score.Score < 0 ? nullptr : game->SessionManager->GetPlayerNumber(score.CommanderId);

        // Port-only (Redux HD): placed among the commanders listed, so a player gone before the results (no name left
        // to show) leaves no gap ("2. Falcon" alone).
        if (player != nullptr)
        {
            lines.push_back({.Place = static_cast<int32_t>(lines.size()) + 1,
                             .Name = player->Name,
                             .Number = score.CommanderId,
                             .Score = score.Score,
                             .Team = TeamOf(results, score.CommanderId, game->HomeTeam)});
        }
    }

    return lines;
}

auto MCMissionEnd::Kills(const MCMechWarrior& warrior) -> int32_t
{
    int32_t kills = 0;

    for (int32_t kind = 0; kind < KillKinds; kind++)
    {
        kills += warrior.NumKilled[kind][1];
    }

    return kills;
}

auto MCMissionEnd::MatchTime() -> std::string
{
    const auto seconds = static_cast<int32_t>(std::fmod(static_cast<double>(ActualTime), SecondsAMinute));
    return std::format("{:02}:{:02}", static_cast<int32_t>(ActualTime) / SecondsAMinute, seconds);
}

auto MCMissionEnd::Lost() -> bool
{
    return MultiPlayer() != nullptr ? HomeSideLost(ScenarioResult, HomeTeam()->Alignment) : ScenarioResult <= WonAbove;
}

auto MCMissionEnd::SayResult() -> void
{
    // Single player's Betty counts a result of 3 lost, as its way back does (1998's label alone took it for a win).
    const bool lost =
        MultiPlayer() != nullptr ? HomeSideLost(ScenarioResult, HomeTeam()->Alignment) : ScenarioResult <= WonAbove;
    SoundSystem()->PlayBettySample(lost ? LostLine : WonLine);
}

auto MCMissionEnd::LeaveBattle() -> void
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    // Port-only (Redux HD): a battle over, this game's leave ends its session for everyone, as the original's sessions
    // ended with each battle; a game that quit a battle still going on hands its session on (MCSessionManager::HandsOn).
    if (game != nullptr && game->SessionManager != nullptr)
    {
        game->SessionManager->HandsOn = Mission()->EndScenarioRequested < 0;

        if (Mission()->EndScenarioRequested != 0)
        {
            game->LeaveSession();
            game->InMission = true;
        }
    }

    SomethingOnFire = 0;
}

auto MCMissionEnd::End() -> void
{
    EventsToMissionResultsScreen = 0;
}

auto MCMissionEnd::ShowsTonnageBonus() -> bool
{
    std::shared_ptr<const MCScenario> scenario = Scenario();
    const MCScenarioObjective& bonus = scenario->Objectives[scenario->Objectives.Count()];
    return MultiPlayer() == nullptr && bonus.Type == MCScenarioObjective::TonnageBonus && bonus.Points > 0 &&
           ScenarioResult > WonAbove && Solo == 0;
}

auto MCMissionEnd::TimesOut() -> bool
{
    return MultiPlayer() != nullptr && IsMPlayerGame != 0;
}

auto MCMissionEnd::CountsResourcePoints() -> bool
{
    return ScenarioResult > WonAbove && Solo == 0;
}
