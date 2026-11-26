#include "stdafx.h"
#include "mission/MCResultsMenu.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCChatView.h"
#include "logistics/MCMatchMaps.h"
#include "logistics/MCRoomScreen.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCResultsReads.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRoomEnd.h"
#include "object/MCBattleMech.h"
#include "object/MCForces.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/SystemInterface.h>

namespace
{
    constexpr const char* ScreenDocument = "results.rml";
    constexpr const char* ScreenModel = "results";
    /// <summary>The document's buttons (ui/results.rml).</summary>
    constexpr const char* MoveOnId = "move-on";
    constexpr const char* RematchId = "rematch";

    /// <summary>The game's words: buffer, the warriors' ranks, an empty salvage, the best pilot's side.</summary>
    constexpr int StringBuffer = 0xfe;
    constexpr uint32_t RankStrings = 0x70;
    constexpr int32_t HighestRank = 3;
    constexpr uint32_t NoSalvageString = 0x361;
    constexpr uint32_t FriendlyString = 0xb6;
    constexpr uint32_t EnemyString = 0xb7;

    /// <summary>Warrior status of a dead pilot; the wounds past which 1998's results call a pilot injured.</summary>
    constexpr int32_t PilotDead = 4;
    constexpr float WoundedPast = 4.0f;
    /// <summary>A pilot's health in pips (six wounds is death).</summary>
    constexpr float HealthPips = 6.0f;
    constexpr float WholePercent = 100.0f;
    constexpr double MsASecond = 1000.0;

    /// <summary>The objectives' marks.</summary>
    constexpr std::string_view DoneState = "done";
    constexpr std::string_view FailedState = "failed";
    constexpr std::string_view OpenState = "open";

    MCGuiHandle<MCResultsScreen>& TheScreen()
    {
        static MCGuiHandle<MCResultsScreen> screen;
        return screen;
    }

    /// <summary>The mission's eyebrow, read as its load starts (MCResultsMenu::BeforeMission).</summary>
    std::string& MissionEyebrow()
    {
        static std::string eyebrow;
        return eyebrow;
    }

    double RmlNow()
    {
        Rml::SystemInterface* system = Rml::GetSystemInterface();
        return system != nullptr ? system->GetElapsedTime() : 0.0;
    }

    std::string GameWord(uint32_t id)
    {
        return LoadGameString(id, StringBuffer);
    }

    std::string Points(int32_t points)
    {
        return std::format(MCResultsMenu::PointsFormat, MCGroupedPoints(points));
    }

    std::string Tons(float tons)
    {
        return std::format(MCResultsMenu::TonsFormat, std::lround(tons));
    }

    /// <summary>A skill's place on its meter, a percentage of the skills' range (1998's bar: 55 pixels across it).</summary>
    float SkillPercent(int32_t skill)
    {
        const float range = MaxPilotSkill - MinPilotSkill;
        return range > 0.0f
                   ? std::clamp((static_cast<float>(skill) - MinPilotSkill) / range * WholePercent, 0.0f, WholePercent)
                   : 0.0f;
    }

    /// <summary>The mover <paramref name="warrior"/> drove, if it's still there.</summary>
    std::shared_ptr<MCMover> MoverOf(const MCMechWarrior& warrior)
    {
        const std::shared_ptr<MCGameObject> vehicle = LockObject(warrior.Vehicle);
        return vehicle != nullptr && IsMoverClass(vehicle->ObjectClass) ? std::static_pointer_cast<MCMover>(vehicle)
                                                                        : nullptr;
    }

    /// <summary>A 'Mech's outline, whole, as the store draws it (none for another mover).</summary>
    std::string Outline(const MCMover& mover)
    {
        if (mover.ObjectClass != MCObjectClass::BattleMech)
        {
            return std::string(MCCampaignScreen::NoPicture);
        }

        const auto& mech = static_cast<const MCBattleMech&>(mover);
        return MCCampaignMenu::Pictured(MCRmlArt::WholeUnitSource(true, mech.NameIndex));
    }

    std::string Face(const MCMechWarrior& warrior)
    {
        return MCCampaignMenu::Pictured(MCCampaignMenu::PilotSource(warrior.Picture));
    }
}

MCResultsScreen::MCResultsScreen()
{
    SetDocument(ScreenDocument);
    // Escape skips the count, as 1998's did; the results' only ways on are their buttons.
    Back = [this] { Skip(); };
    // A match's best pilot's pictures, bound in single player too (an empty decorator is no picture's syntax error).
    _BestMech = MCCampaignScreen::NoPicture;
    _BestFace = MCCampaignScreen::NoPicture;
}

MCResultsScreen::~MCResultsScreen() = default;

void MCResultsScreen::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, ScreenModel);
    MCRmlModel& model = *_Model;
    model.Value("match", _Match);
    model.Value("eyebrow", _Eyebrow);
    model.Value("title", _Title);
    model.Value("earned", _Earned);
    model.Value("moveOn", _MoveOn);
    model.Value("rematch", _RematchShown);
    model.Value("time", _MatchTime);
    model.Value("salvageShown", _SalvageShown);
    model.Value("noSalvage", _NoSalvage);
    model.Value("noSalvageText", _NoSalvageText);
    model.List("figures", _Figures, std::pair{"n", &MCFigure::Number}, std::pair{"what", &MCFigure::What},
               std::pair{"shown", &MCFigure::Shown});
    model.List("objectives", _Objectives, std::pair{"name", &MCObjectiveRow::Name},
               std::pair{"points", &MCObjectiveRow::Points}, std::pair{"state", &MCObjectiveRow::State},
               std::pair{"head", &MCObjectiveRow::Head}, std::pair{"shown", &MCObjectiveRow::Shown});
    model.List("salvage", _Salvage, std::pair{"picture", &MCSalvageRow::Picture},
               std::pair{"name", &MCSalvageRow::Name}, std::pair{"note", &MCSalvageRow::Note});
    // The skills' list first: the pilots' rows hold their skills (RmlUi's register then knows a list of skills).
    model.List("skillTypes", _NoSkills, std::pair{"value", &MCSkill::Value}, std::pair{"gain", &MCSkill::Gain},
               std::pair{"fill", &MCSkill::Fill}, std::pair{"gained", &MCSkill::Gained});
    model.List("pilots", _Pilots, std::pair{"face", &MCPilotRow::Face}, std::pair{"name", &MCPilotRow::Name},
               std::pair{"status", &MCPilotRow::Status}, std::pair{"statusColour", &MCPilotRow::StatusColour},
               std::pair{"mech", &MCPilotRow::Mech}, std::pair{"kills", &MCPilotRow::Kills},
               std::pair{"rank", &MCPilotRow::Rank}, std::pair{"promoted", &MCPilotRow::Promoted},
               std::pair{"gone", &MCPilotRow::Gone}, std::pair{"shown", &MCPilotRow::Shown},
               std::pair{"skills", &MCPilotRow::Skills});
    model.List("places", _Places, std::pair{"at", &MCPlaceRow::At}, std::pair{"name", &MCPlaceRow::Name},
               std::pair{"colour", &MCPlaceRow::Colour}, std::pair{"side", &MCPlaceRow::Side},
               std::pair{"kills", &MCPlaceRow::Kills});
    model.List("list", _List, std::pair{"name", &MCListRow::Name}, std::pair{"mech", &MCListRow::Mech},
               std::pair{"who", &MCListRow::Who}, std::pair{"whoColour", &MCListRow::WhoColour},
               std::pair{"kills", &MCListRow::Kills});
    model.Value("best", _Best);
    model.Value("bestWho", _BestWho);
    model.Value("bestWhat", _BestWhat);
    model.Value("bestTons", _BestTons);
    model.Value("bestMech", _BestMech);
    model.Value("bestFace", _BestFace);
    model.Value("bestHealth", _BestHealth);
    model.Value("bestKills", _BestKills);
    model.Value("bestSide", _BestSide);
    model.Value("bestCommander", _BestCommander);
    model.Value("bestColour", _BestColour);
}

void MCResultsScreen::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick(MoveOnId, [this] { MoveOn(); });
    OnClick(RematchId, [this] { Rematch(); });
}

void MCResultsScreen::OnDestroy()
{
    if (_Begun)
    {
        MCMissionEnd::End();
    }

    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCResultsScreen::SetUp()
{
    _Match = MultiPlayer() != nullptr;
    _Begun = true;
    MCMissionEnd::Begin();
    _Count.Start();
    _Results = MCMissionEnd::Gather();
    const bool lost = MCMissionEnd::Lost();
    _Title = lost ? MCResultsMenu::LostTitle : MCResultsMenu::WonTitle;
    _Eyebrow = MissionEyebrow();
    _MoveOn = _Match ? MCResultsMenu::DoneWord
              : lost ? MCResultsMenu::ReplayMissionWord
                     : MCResultsMenu::NextMissionWord;
    Keys = {{"Enter", _MoveOn}};
    _Figures.clear();

    const std::array<int32_t, MCResultsMenu::FigureNames.size()> values = _Results.Statistics.Values();

    for (size_t at = 0; at < MCResultsMenu::FigureNames.size(); ++at)
    {
        _Figures.push_back(
            MCFigure{.Number = std::to_string(values[at]), .What = std::string(MCResultsMenu::FigureNames[at])});
    }

    // A match's rows read the session's names, before its session is left.
    if (_Match)
    {
        FillMatch();
    }
    else
    {
        FillSinglePlayer();
    }

    MCMissionEnd::SayResult();
    MCMissionEnd::LeaveBattle();
    _RematchShown = _Match && MCRoomEnd::OffersRematch();
    _ClosesAt = MCMissionEnd::TimesOut()
                    ? std::optional<double>(RmlNow() + static_cast<double>(MCMissionEnd::MatchTimeout) / MsASecond)
                    : std::nullopt;
    ShowCounted();

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCResultsScreen::FillSinglePlayer()
{
    _Earned = MCMissionEnd::CountsResourcePoints() ? MCGroupedPoints(0) : std::string();
    const MCObjectiveList& objectives = Scenario()->Objectives;
    _Objectives.clear();
    _ObjectiveSteps.clear();
    bool secondaryHead = false;

    // As the 1998 screen lists them: each step's objectives of its type, 0 .. count, the tonnage bonus after the
    // secondary ones; an objective neither done, failed nor open isn't listed.
    for (const MCResultsStep step : {MCResultsStep::PrimaryObjectives, MCResultsStep::SecondaryObjectives})
    {
        const uint32_t wantedType =
            step != MCResultsStep::PrimaryObjectives ? MCScenarioObjective::Secondary : MCScenarioObjective::Primary;

        for (int32_t index = 0; index <= objectives.Count(); index++)
        {
            const MCScenarioObjective& objective = objectives[index];
            const std::string_view state = objective.Status == MCScenarioObjective::Succeeded ? DoneState
                                           : objective.Status == MCScenarioObjective::Failed  ? FailedState
                                           : objective.Status == MCScenarioObjective::Pending ? OpenState
                                                                                              : std::string_view();

            if (objective.Type == wantedType && !state.empty())
            {
                MCObjectiveRow row{.Name = objective.Name,
                                   .Points = objective.Points != 0 ? Points(objective.Points) : std::string(),
                                   .State = std::string(state)};

                if (wantedType == MCScenarioObjective::Secondary && !secondaryHead)
                {
                    row.Head = MCResultsMenu::SecondaryHead;
                    secondaryHead = true;
                }

                _Objectives.push_back(std::move(row));
                _ObjectiveSteps.emplace_back(step, index);
            }

            if (index == objectives.Count() && step == MCResultsStep::SecondaryObjectives &&
                MCMissionEnd::ShowsTonnageBonus())
            {
                const MCScenarioObjective& bonus = objectives[objectives.Count()];
                _Objectives.push_back(MCObjectiveRow{.Name = bonus.Name,
                                                     .Points = Points(bonus.Points),
                                                     .State = std::string(DoneState),
                                                     .Head = std::string(MCResultsMenu::BonusHead)});
                _ObjectiveSteps.emplace_back(step, index);
            }
        }
    }

    // What the campaign gets (the bridge: the 'Mechs salvaged, and the parts from what was salvaged, each part summed).
    _Salvage.clear();
    std::vector<std::pair<uint8_t, int32_t>> parts;

    for (const std::shared_ptr<MCGameObject>& object : MCResultsReads::Salvage())
    {
        if (object->ObjectClass == MCObjectClass::BattleMech)
        {
            auto& mech = static_cast<MCMover&>(*object);
            _Salvage.push_back(MCSalvageRow{
                .Picture = Outline(mech), .Name = std::string(mech.GetIfaceName()), .Note = Tons(mech.TonnageClass)});
        }

        for (const MCSalvageItem& item : object->GetSalvage())
        {
            const auto part = std::ranges::find(parts, item.ItemId, &std::pair<uint8_t, int32_t>::first);

            if (part != parts.end())
            {
                part->second += item.NumItems;
            }
            else
            {
                parts.emplace_back(item.ItemId, item.NumItems);
            }
        }
    }

    for (const auto& [id, count] : parts)
    {
        _Salvage.push_back(MCSalvageRow{.Picture = std::string(MCCampaignScreen::NoPicture),
                                        .Name = MasterComponentList[id].Name,
                                        .Note = std::format(MCResultsMenu::SalvagedFormat, count)});
    }

    _NoSalvageText = MCRmlDialogs::SentenceCase(GameWord(NoSalvageString)) + ".";
    _Pilots.clear();

    for (const MCMissionPilotResult& result : _Results.Pilots)
    {
        const std::shared_ptr<MCMechWarrior> warrior = result.Warrior.lock();

        if (warrior == nullptr)
        {
            _Pilots.emplace_back();
            continue;
        }

        const std::shared_ptr<MCMover> mover = MoverOf(*warrior);
        const int32_t rank = static_cast<int8_t>(warrior->Rank);
        const bool dead = warrior->Status == PilotDead;
        MCPilotRow row{.Face = Face(*warrior),
                       .Name = warrior->Callsign,
                       .Status = std::string(dead                            ? MCResultsMenu::KilledWord
                                             : warrior->Wounds > WoundedPast ? MCResultsMenu::WoundedWord
                                                                             : std::string_view()),
                       .StatusColour = dead ? "red" : "orange",
                       .Mech = mover != nullptr ? std::string(mover->GetIfaceName()) : std::string(),
                       .Kills = std::to_string(MCMissionEnd::Kills(*warrior)),
                       .Rank = rank >= 0 && rank <= HighestRank ? GameWord(RankStrings + static_cast<uint32_t>(rank))
                                                                : std::string(),
                       .Promoted = result.OldRank < rank,
                       .Gone = dead};

        for (const int32_t skill : ResultsSkillOrder)
        {
            const int32_t before = result.Skills[static_cast<size_t>(skill)];
            const auto now = static_cast<int32_t>(warrior->SkillRank[skill]);
            row.Skills.push_back(MCSkill{.Value = std::to_string(now),
                                         .Gain = now > before ? std::format("+{}", now - before) : std::string(),
                                         .Fill = SkillPercent(before),
                                         .Gained = std::max(SkillPercent(now) - SkillPercent(before), 0.0f)});
        }

        _Pilots.push_back(std::move(row));
    }
}

void MCResultsScreen::FillMatch()
{
    _Earned.clear();
    _Objectives.clear();
    _ObjectiveSteps.clear();
    _Salvage.clear();
    _Pilots.clear();
    _MatchTime = MCMissionEnd::MatchTime();
    const std::shared_ptr<MCTeam> home = HomeTeam();
    const std::vector<MCMissionEnd::MCCommanderLine> commanders = MCMissionEnd::CommanderLines(_Results);
    const auto commander = [&commanders](int32_t number) -> MCOptionalRef<const MCMissionEnd::MCCommanderLine>
    {
        const auto found = std::ranges::find(commanders, number, &MCMissionEnd::MCCommanderLine::Number);
        return found != commanders.end() ? MCOptionalRef<const MCMissionEnd::MCCommanderLine>(*found) : std::nullopt;
    };

    _Places.clear();

    for (const MCMissionEnd::MCCommanderLine& line : commanders)
    {
        _Places.push_back(
            MCPlaceRow{.At = std::format("{}.", line.Place),
                       .Name = line.Name,
                       .Colour = MCChatView::ColourOf(line.Number),
                       .Side = line.Team > 0 ? std::format(MCResultsMenu::TeamFormat, line.Team) : std::string(),
                       .Kills = std::to_string(line.Score)});
    }

    _List.clear();
    _Best = false;

    for (size_t at = 0; at < _Results.Pilots.size(); ++at)
    {
        const MCMissionPilotResult& result = _Results.Pilots[at];
        const std::shared_ptr<MCMechWarrior> warrior = result.Warrior.lock();
        const std::shared_ptr<MCMover> mover = warrior != nullptr ? MoverOf(*warrior) : nullptr;

        if (mover == nullptr)
        {
            continue;
        }

        const MCOptionalRef<const MCMissionEnd::MCCommanderLine> who = commander(mover->GetCommanderId());
        const std::string whoName = who.has_value() ? who->get().Name : std::string();
        const std::string whoColour = who.has_value() ? MCChatView::ColourOf(who->get().Number) : std::string();
        _List.push_back(MCListRow{.Name = mover->NetName,
                                  .Mech = std::string(mover->GetIfaceName()),
                                  .Who = whoName,
                                  .WhoColour = whoColour,
                                  .Kills = std::to_string(MCMissionEnd::Kills(*warrior))});

        // The best pilot: the list's first, by kills (1998's). Port fix as the 1998 screen's: none when there's none.
        if (at == 0)
        {
            const int32_t kills = result.OldRank;
            _Best = true;
            _BestWho = mover->NetName;
            _BestWhat = mover->GetIfaceName();
            _BestTons = Tons(mover->TonnageClass);
            _BestMech = Outline(*mover);
            _BestFace = Face(*warrior);
            _BestHealth = static_cast<int>(std::clamp(std::round(HealthPips - warrior->Wounds), 0.0f, HealthPips));
            _BestKills =
                kills == 1 ? std::string(MCResultsMenu::OneKill) : std::format(MCResultsMenu::KillsFormat, kills);
            _BestSide =
                GameWord(home != nullptr && warrior->Alignment == home->Alignment ? FriendlyString : EnemyString);
            _BestCommander = whoName;
            _BestColour = whoColour;
        }
    }

    // The battle's numbers at once (1998's match screen drew them whole).
    for (MCFigure& figure : _Figures)
    {
        figure.Shown = true;
    }
}

bool MCResultsScreen::ShowCounted()
{
    if (_Match)
    {
        return false;
    }

    bool changed = false;
    const auto set = [&changed](auto& value, auto now)
    {
        if (value != now)
        {
            value = std::move(now);
            changed = true;
        }
    };

    if (MCMissionEnd::CountsResourcePoints())
    {
        set(_Earned, MCGroupedPoints(std::max(_Count.ShownResourcePoints(), 0)));
    }

    const int32_t statistics = _Count.StatisticsShown();

    for (size_t at = 0; at < _Figures.size(); ++at)
    {
        set(_Figures[at].Shown, static_cast<int32_t>(at) < statistics);
    }

    for (size_t at = 0; at < _Objectives.size(); ++at)
    {
        set(_Objectives[at].Shown, _Count.ObjectiveShown(_ObjectiveSteps[at].first, _ObjectiveSteps[at].second));
    }

    const int32_t pilots = _Count.PilotsShown(_Pilots.size());

    for (size_t at = 0; at < _Pilots.size(); ++at)
    {
        set(_Pilots[at].Shown, static_cast<int32_t>(at) < pilots);
    }

    // The salvage with the debriefing, as 1998's text box came up.
    set(_NoSalvage, _Count.Finished() && _Salvage.empty());
    set(_SalvageShown, _Count.Finished());
    return changed;
}

void MCResultsScreen::OnFrame()
{
    if (!_Begun)
    {
        return;
    }

    if (!_Match)
    {
        _Count.Run(_Results);
    }

    if (ShowCounted() && _Model != nullptr)
    {
        _Model->Changed();
    }

    // A launcher's match: its results close by themselves (1998's 90 seconds), as a press of DONE.
    if (_ClosesAt.has_value() && RmlNow() >= *_ClosesAt)
    {
        _ClosesAt.reset();
        SDL_Log("Results: a launcher's match's results close after %u s", MCMissionEnd::MatchTimeout / 1000);
        Mission()->LeaveResults();
    }
}

void MCResultsScreen::Skip()
{
    _Count.Skip();
}

void MCResultsScreen::MoveOn()
{
    // Port-only (Redux HD): a rematch under way closes them itself (network/MCRoomEnd.h).
    if (!MCRoomEnd::Rematching())
    {
        Mission()->LeaveResults();
    }
}

void MCResultsScreen::Rematch()
{
    if (!_RematchShown)
    {
        return;
    }

    _RematchShown = false;

    if (_Model != nullptr)
    {
        _Model->Changed("rematch");
    }

    MCRoomEnd::Rematch();
}

bool MCResultsScreen::Finished() const
{
    return _Match || _Count.Finished();
}

// MCResultsMenu

void MCResultsMenu::BeforeMission()
{
    std::string& eyebrow = MissionEyebrow();
    eyebrow.clear();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (logistics == nullptr)
    {
        return;
    }

    const MCCampaignMenu::MCMissionRead mission = MCCampaignMenu::ReadMission();

    // A match: its room (LAN, Internet, a launcher's) and the room's name, then the mission's; a campaign's mission:
    // its operation and number, then its name; a solo mission its operation's name, then its own.
    if (MultiPlayer() != nullptr)
    {
        eyebrow =
            std::format(NamedFormat, std::format(NamedFormat, RoomPlace(), logistics->Room.Name()), mission.Title);
    }
    else if (mission.Mission > 0)
    {
        eyebrow = std::format(EyebrowFormat, mission.Operation, mission.Mission, mission.Title);
    }
    else
    {
        eyebrow = mission.OperationName.empty() ? mission.Title
                                                : std::format(NamedFormat, mission.OperationName, mission.Title);
    }
}

void MCResultsMenu::Show()
{
    MCResultsScreen& screen = MCRmlFlow::Needed(TheScreen(), "the results", MCRmlFlow::MCOn::Mission);
    screen.SetUp();
    screen.Layer().Context().Update();
    screen.Show();
}

bool MCResultsMenu::Up()
{
    const MCOptionalRef<MCResultsScreen> screen = Screen();
    return screen.has_value() && screen->get().Shown();
}

bool MCResultsMenu::Finished()
{
    const MCOptionalRef<MCResultsScreen> screen = Screen();
    return screen.has_value() && screen->get().Finished();
}

void MCResultsMenu::Close()
{
    if (const std::shared_ptr<MCResultsScreen> screen = TheScreen().Lock(); screen != nullptr)
    {
        screen->Close();
    }

    TheScreen().reset();
}

MCOptionalRef<MCResultsScreen> MCResultsMenu::Screen()
{
    return MCRmlFlow::Alive(TheScreen());
}
