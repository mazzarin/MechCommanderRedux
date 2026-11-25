#pragma once

// Port-only (SPEC-menus.md, "The results"; stage 9, step 1): the mission's results as designed, one RmlUi screen
// (ui/results.rml) over the whole battlefield as it ended, on the mission's layer at its INTERFACE SIZE. Single
// player: the mission's name and MISSION SUCCESSFUL or MISSION FAILED, the RP earned, the battle's five numbers, the
// objectives and the salvage at the left, every pilot who dropped at the right, counted up a step at a time as 1998's
// (MCResultsCount; Escape shows the rest at once), then NEXT MISSION or REPLAY MISSION. A match's: its numbers for the
// player's team, the commanders by kills, the best pilot, every pilot by kills; REMATCH (the host's, when a guest can
// have it) and DONE. What it shows is worked out once (mission/MCMissionEnd, as the 1998 screen's), what it reads of the
// battle's interface through mission/MCResultsReads; its presses go the 1998 screen's ways (MCMission::LeaveResults,
// MCRoomEnd::Rematch). The 1998 screen (mission/MCMissionResultsScreen) is no longer made.

#include "mission/MCMissionEnd.h"
#include "ui/MCRmlScreen.h"

class MCRmlModel;

/// <summary>The results screen.</summary>
class MCResultsScreen final : public MCRmlScreen
{
public:
    /// <summary>One of the battle's five numbers: blank until the count reaches it.</summary>
    struct MCFigure
    {
        std::string Number;
        std::string What;
    };

    /// <summary>An objective's row (or the tonnage bonus's): its mark, name and RP, hidden until counted.</summary>
    struct MCObjectiveRow
    {
        std::string Name;
        std::string Points;
        /// <summary>The mark: done (a tick), failed (a cross), or neither (still open as the mission ended).</summary>
        std::string State;
        /// <summary>The heading over it, if it starts a group ("Secondary", "Bonus").</summary>
        std::string Head;
        bool Shown = false;
    };

    /// <summary>What the mission salvaged for the campaign: a 'Mech, or a part from what was salvaged.</summary>
    struct MCSalvageRow
    {
        /// <summary>Its picture as a decorator (none: no picture).</summary>
        std::string Picture;
        std::string Name;
        std::string Note;
    };

    /// <summary>A pilot's skill: its number, its gain, the meter filled to the old value and the gain beside it.</summary>
    struct MCSkill
    {
        std::string Value;
        std::string Gain;
        float Fill = 0.0f;
        float Gained = 0.0f;
    };

    /// <summary>A single-player pilot's row.</summary>
    struct MCPilotRow
    {
        /// <summary>The face as a decorator.</summary>
        std::string Face;
        std::string Name;
        /// <summary>"Wounded", "Killed" or none, and its colour class.</summary>
        std::string Status;
        std::string StatusColour;
        std::string Mech;
        std::string Kills;
        std::string Rank;
        bool Promoted = false;
        bool Gone = false;
        bool Shown = false;
        std::vector<MCSkill> Skills;
    };

    /// <summary>A match's commander by kills.</summary>
    struct MCPlaceRow
    {
        std::string At;
        std::string Name;
        std::string Colour;
        std::string Side;
        std::string Kills;
    };

    /// <summary>A match's pilot in the list by kills.</summary>
    struct MCListRow
    {
        std::string Name;
        std::string Mech;
        std::string Who;
        std::string WhoColour;
        std::string Kills;
    };

    MCResultsScreen();
    ~MCResultsScreen() override;

    /// <summary>
    /// The results worked out and shown as the scenario ends (MCMissionEnd: the battle's callbacks stopped, the numbers
    /// gathered once, Betty's line, a match's session handed on or left), the count started.
    /// </summary>
    void SetUp();

    /// <summary>Escape: the rest of the count shown at once, silently.</summary>
    void Skip();

    /// <summary>The way on (NEXT MISSION, REPLAY MISSION, DONE): out of the mission, unless a rematch is under way.</summary>
    void MoveOn();
    /// <summary>REMATCH: it goes, and the rematch begins (network/MCRoomEnd.h).</summary>
    void Rematch();

    /// <summary>Whether every step of the count is shown (a match's: at once).</summary>
    bool Finished() const;

    /// <summary>What shows (the tests read it).</summary>
    const std::string& Eyebrow() const { return _Eyebrow; }
    const std::string& Title() const { return _Title; }
    const std::string& Earned() const { return _Earned; }
    const std::string& MoveOnWord() const { return _MoveOn; }
    bool RematchShown() const { return _RematchShown; }
    const std::vector<MCFigure>& Figures() const { return _Figures; }
    const std::vector<MCObjectiveRow>& Objectives() const { return _Objectives; }
    const std::vector<MCSalvageRow>& Salvage() const { return _Salvage; }
    const std::vector<MCPilotRow>& Pilots() const { return _Pilots; }
    const std::vector<MCPlaceRow>& Places() const { return _Places; }
    const std::vector<MCListRow>& List() const { return _List; }
    const std::string& MatchTime() const { return _MatchTime; }
    const MCMissionResults& Results() const { return _Results; }

    void OnFrame() override;
    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;

private:
    /// <summary>Single player's rows, and a match's.</summary>
    void FillSinglePlayer();
    void FillMatch();
    /// <summary>What the count has reached shown; whether anything changed.</summary>
    bool ShowCounted();

    std::unique_ptr<MCRmlModel> _Model;
    /// <summary>The end begun (<see cref="SetUp"/>): ended as the screen goes.</summary>
    bool _Begun = false;
    MCMissionResults _Results;
    MCResultsCount _Count;
    bool _Match = false;
    std::string _Eyebrow;
    std::string _Title;
    std::string _Earned;
    std::string _MoveOn;
    bool _RematchShown = false;
    std::vector<MCFigure> _Figures;
    std::vector<MCObjectiveRow> _Objectives;
    /// <summary>Each objective row's place in the count (its step and index), as the 1998 screen lists them.</summary>
    std::vector<std::pair<MCResultsStep, int32_t>> _ObjectiveSteps;
    std::vector<MCSalvageRow> _Salvage;
    /// <summary>The salvage shown (with the debriefing, the count's last step), and the words for none.</summary>
    bool _SalvageShown = false;
    bool _NoSalvage = false;
    std::string _NoSalvageText;
    std::vector<MCPilotRow> _Pilots;
    /// <summary>Bound to declare the skills' row type before the pilots' (it stays empty).</summary>
    std::vector<MCSkill> _NoSkills;
    std::vector<MCPlaceRow> _Places;
    std::vector<MCListRow> _List;
    std::string _MatchTime;
    /// <summary>A match's best pilot: the card's lines and pictures, the kills, and the side and commander.</summary>
    bool _Best = false;
    std::string _BestWho;
    std::string _BestWhat;
    std::string _BestTons;
    std::string _BestMech;
    std::string _BestFace;
    int _BestHealth = 0;
    std::string _BestKills;
    std::string _BestSide;
    std::string _BestCommander;
    std::string _BestColour;
    /// <summary>A launcher's match closes its results by itself (MCMissionEnd::TimesOut), from when they came up.</summary>
    std::optional<double> _ClosesAt;
};

namespace MCResultsMenu
{
    /// <summary>
    /// The mission's names the results show, read as its load starts while logistics is there (its briefing, a match's
    /// room): the eyebrow over the title. None for a mission started without logistics (a game segment).
    /// </summary>
    void BeforeMission();

    /// <summary>
    /// The results shown at the scenario's end (<see cref="MCResultsScreen::SetUp"/>). Port-only (stage 9): where they
    /// can't be read, the game stops (MCRmlFlow::Broken): there is no other screen to end the mission on.
    /// </summary>
    void Show();

    /// <summary>Whether the remade results are up, and whether their count is done.</summary>
    bool Up();
    bool Finished();

    /// <summary>The results closed (the way out of the mission, the game's end).</summary>
    void Close();

    /// <summary>The screen while the menus have it.</summary>
    MCOptionalRef<MCResultsScreen> Screen();

    /// <summary>The words.</summary>
    inline constexpr std::string_view WonTitle = "Mission successful";
    inline constexpr std::string_view LostTitle = "Mission failed";
    inline constexpr std::string_view NextMissionWord = "Next mission";
    inline constexpr std::string_view ReplayMissionWord = "Replay mission";
    inline constexpr std::string_view DoneWord = "Done";
    /// <summary>A campaign mission's eyebrow (the won frame's: it names the mission), a solo mission's, a match's.</summary>
    inline constexpr std::string_view EyebrowFormat = "Operation {} · Mission {} · {}";
    inline constexpr std::string_view NamedFormat = "{} · {}";
    inline constexpr std::string_view PointsFormat = "{} RP";
    inline constexpr std::string_view TonsFormat = "{} tons";
    /// <summary>A part salvaged: how many.</summary>
    inline constexpr std::string_view SalvagedFormat = "{} salvaged";
    inline constexpr std::string_view SecondaryHead = "Secondary";
    inline constexpr std::string_view BonusHead = "Bonus";
    inline constexpr std::string_view WoundedWord = "Wounded";
    inline constexpr std::string_view KilledWord = "Killed";
    inline constexpr std::string_view TeamFormat = "Team {}";
    inline constexpr std::string_view KillsFormat = "{} kills";
    inline constexpr std::string_view OneKill = "1 kill";
    /// <summary>The five numbers' names, in the count's order.</summary>
    inline constexpr std::array<std::string_view, 5> FigureNames = {"Enemy units destroyed", "Enemy 'Mechs destroyed",
                                                                    "Enemy pilots killed", "Your units lost",
                                                                    "Your 'Mechs lost"};
}
