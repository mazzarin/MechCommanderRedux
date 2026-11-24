#pragma once

#include "mission/MCMissionResults.h"

class MCMechWarrior;

// Port-only (SPEC-menus.md's stage 9): the mission's end as its results take it, out of the 1998 results screen
// (mission/MCMissionResultsScreen) so the remade results (mission/MCResultsMenu) do exactly what it does: the battle's
// callbacks stopped, what the results show worked out once (the skill-ups and promotions applied: MCMissionResults),
// Betty's line, a match's session left, and the count a step at a time. Every view of the results calls these; none
// changes what the mission awards.

/// <summary>The steps the single-player results count through, each shown a line at a time.</summary>
enum class MCResultsStep : int32_t
{
    /// <summary>The resource points count up.</summary>
    ResourcePoints,
    /// <summary>The kill and loss statistics.</summary>
    Statistics,
    /// <summary>The primary objectives.</summary>
    PrimaryObjectives,
    /// <summary>The secondary objectives, then the tonnage bonus.</summary>
    SecondaryObjectives,
    /// <summary>The pilots.</summary>
    Pilots,
    /// <summary>The debriefing text comes up.</summary>
    Debriefing,
    /// <summary>Everything is shown.</summary>
    Done
};

/// <summary>Ticks between two steps of the results (20).</summary>
extern uint32_t ResultsStepTicks;

/// <summary>
/// The single-player results' count, a step at a time (each due <see cref="ResultsStepTicks"/> after the last, as the
/// 1998 screen timed them on MouseTicks, with its sounds); Escape (<see cref="Skip"/>) shows the rest at once, silently.
/// What a view shows is what the count has reached (<see cref="StatisticsShown"/>, <see cref="ObjectiveShown"/>,
/// <see cref="PilotsShown"/>).
/// </summary>
class MCResultsCount
{
public:
    /// <summary>The count from its start, its first step due now.</summary>
    void Start();
    /// <summary>The steps that are due taken (all of them once skipping), for <paramref name="results"/>.</summary>
    void Run(const MCMissionResults& results);
    /// <summary>Escape: the rest shown at once, without sounds.</summary>
    void Skip() { _Skipping = true; }

    MCResultsStep Step() const { return _Step; }
    int32_t StepIndex() const { return _StepIndex; }
    bool Skipping() const { return _Skipping; }
    /// <summary>Whether every step is shown (the debriefing is up).</summary>
    bool Finished() const { return _Step == MCResultsStep::Done; }
    /// <summary>The resource points the count shows; -1 before the count starts.</summary>
    int32_t ShownResourcePoints() const { return _ShownResourcePoints; }
    /// <summary>How many of the five statistics are shown.</summary>
    int32_t StatisticsShown() const;
    /// <summary>Whether objective <paramref name="index"/> of <paramref name="step"/>'s list is reached.</summary>
    bool ObjectiveShown(MCResultsStep step, int32_t index) const;
    /// <summary>How many of the <paramref name="lines"/> pilot lines are shown.</summary>
    int32_t PilotsShown(size_t lines) const;

    /// <summary>A pilot's line comes up (the 1998 screen takes its card's picture then).</summary>
    std::function<void(size_t pilot)> OnPilot;
    /// <summary>The debriefing comes up (the 1998 screen shows its text box then).</summary>
    std::function<void()> OnDebriefing;

private:
    void StepResourcePoints(const MCMissionResults& results);
    void StepStatistics();
    void StepObjectives();
    void StepPilots(const MCMissionResults& results);
    void StepDebriefing();
    void AdvanceStep();
    void Play(uint32_t sample) const;

    MCResultsStep _Step = MCResultsStep::ResourcePoints;
    int32_t _StepIndex = 0;
    /// <summary><c>MouseTicks</c> at which the next step is due; 0 when the steps are finished.</summary>
    uint32_t _NextDrawTime = 0;
    bool _Skipping = false;
    int32_t _ShownResourcePoints = -1;
};

namespace MCMissionEnd
{
    /// <summary>A commander's line on a match's results: its place by kills, the player's name, the kills.</summary>
    struct MCCommanderLine
    {
        /// <summary>The place (1-6) by kills.</summary>
        int32_t Place = 0;
        /// <summary>The player's name (kept: the session may be left before the results close).</summary>
        std::string Name;
        /// <summary>The session's player number (the player's colour).</summary>
        int32_t Number = -1;
        int32_t Score = 0;
        /// <summary>The room's team (1 or 2) the commander's pilots fought for; 0 when it can't be told.</summary>
        int32_t Team = 0;
    };

    /// <summary>
    /// The results come up: they take the game's input (EventsToMissionResultsScreen), the battle's callbacks stop and
    /// its cursor goes.
    /// </summary>
    void Begin();

    /// <summary>
    /// What the results show, worked out once as the scenario ends: a match's numbers, or single player's with the
    /// skill-ups of a win applied (MCMissionResults' Gather functions). Call it once.
    /// </summary>
    MCMissionResults Gather();

    /// <summary>A match's commanders by kills, named while the session is there (before <see cref="LeaveBattle"/>).</summary>
    std::vector<MCCommanderLine> CommanderLines(const MCMissionResults& results);

    /// <summary>A pilot's kills, every kind together (1998's count).</summary>
    int32_t Kills(const MCMechWarrior& warrior);

    /// <summary>A match's length, "mm:ss".</summary>
    std::string MatchTime();

    /// <summary>Whether the mission counts as lost for the player (a match: the player's side).</summary>
    bool Lost();

    /// <summary>Betty's line for the result.</summary>
    void SayResult();

    /// <summary>
    /// A match's battle over: its session handed on or left (a game that quit its battle left it at once), and the
    /// fires' sound stopped.
    /// </summary>
    void LeaveBattle();

    /// <summary>The results closed: the game's input back to it.</summary>
    void End();

    /// <summary>Whether the tonnage bonus is listed after the secondary objectives (single player, won, not solo).</summary>
    bool ShowsTonnageBonus();

    /// <summary>Whether a match's results close by themselves after <see cref="MatchTimeout"/> (a launcher's match).</summary>
    bool TimesOut();
    /// <summary>How long a launcher's match's results stay up, in milliseconds (1998's: 90 seconds).</summary>
    inline constexpr uint32_t MatchTimeout = 90000;

    /// <summary>The single-player results count RP: a win, not a solo mission.</summary>
    bool CountsResourcePoints();
}
