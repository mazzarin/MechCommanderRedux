#include "stdafx.h"
#include "logistics/MCMatchOptionRows.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCWeaponBans.h"
#include "network/MCMultiPlayer.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"

namespace
{
    using Choice = MCOptionChoice;
    using Row = MCOptionRow;

    /// <summary>The rows' values: a weather or time of day (<see cref="MissionOwn"/>: the mission's own), braking on.</summary>
    constexpr int32_t MissionOwn = MCMPMatchOptionsMessage::MissionOwn;
    int WeatherValue = MissionOwn;
    int TimeValue = MissionOwn;
    int BrakingValue = 0;
    int ArtilleryValue = 1;
    int ProbesValue = 1;
    int SensorsValue = 1;
    int MinesValue = 1;
    int PilotValue = static_cast<int>(MCPilotRank::Elite);
    /// <summary>Each weapon family's row (WeaponFamilies' order): 1 ON, the original's, 0 OFF, banned.</summary>
    std::array<int, WeaponFamilies.size()> WeaponValues = []
    {
        std::array<int, WeaponFamilies.size()> on{};
        on.fill(1);
        return on;
    }();

    /// <summary>
    /// Whether the room plays <typeparamref name="Option"/>'s value <typeparamref name="Value"/> at some version (every
    /// game in it has that version's rules: the capability map's): the host's click skips it otherwise.
    /// </summary>
    template <MCMatchOption Option, auto Value> bool RoomPlays()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        return game == nullptr || MCRoomVersion(MCCapabilityOf(Option), static_cast<int32_t>(Value),
                                                game->AgreedRules(MCMatchRule::Supported))
                                      .has_value();
    }

    /// <summary>A weather's or time of day's choice, offered while the room plays it.</summary>
    template <MCMatchOption Option, auto Value> constexpr Choice ChoiceOf(std::string_view name)
    {
        return Choice{name, static_cast<int32_t>(Value), RoomPlays<Option, Value>};
    }

    constexpr std::string_view MissionOwnName = "MISSION'S";
    constexpr std::array WeatherChoices = {Choice{MissionOwnName, MissionOwn},
                                           ChoiceOf<MCMatchOption::Weather, MCWeatherKind::Clear>("CLEAR"),
                                           ChoiceOf<MCMatchOption::Weather, MCWeatherKind::Rain>("RAIN"),
                                           ChoiceOf<MCMatchOption::Weather, MCWeatherKind::Downpour>("DOWNPOUR"),
                                           ChoiceOf<MCMatchOption::Weather, MCWeatherKind::LightSnow>("LIGHT SNOW"),
                                           ChoiceOf<MCMatchOption::Weather, MCWeatherKind::Snow>("SNOW"),
                                           ChoiceOf<MCMatchOption::Weather, MCWeatherKind::Blizzard>("BLIZZARD")};
    constexpr std::array TimeChoices = {Choice{MissionOwnName, MissionOwn},
                                        ChoiceOf<MCMatchOption::TimeOfDay, MCTimeOfDay::Day>("DAY"),
                                        ChoiceOf<MCMatchOption::TimeOfDay, MCTimeOfDay::Dusk>("DUSK"),
                                        ChoiceOf<MCMatchOption::TimeOfDay, MCTimeOfDay::Night>("NIGHT")};
    constexpr std::array OffOn = {Choice{"OFF", 0}, Choice{"ON", 1}};
    /// <summary>The ranks in the game's own words, the most skilled (any pilot, the original's) first.</summary>
    constexpr std::array PilotChoices = {Choice{"ELITE", static_cast<int32_t>(MCPilotRank::Elite)},
                                         Choice{"VETERAN", static_cast<int32_t>(MCPilotRank::Veteran)},
                                         Choice{"REGULAR", static_cast<int32_t>(MCPilotRank::Regular)},
                                         Choice{"GREEN", static_cast<int32_t>(MCPilotRank::Green)}};

    /// <summary>
    /// The host's change of one option (<paramref name="change"/> of its match options and its game): into its options,
    /// and what the match plays of them to every guest. A guest's list changes nothing.
    /// </summary>
    template <typename Change> void HostChanges(Change change)
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

        if (game == nullptr || game->IsHost == 0)
        {
            return;
        }

        change(game->MatchOptions, *game);
        game->SendMatchOptions();
    }

    /// <summary>A row's value as a weather or time of day (<see cref="MissionOwn"/>: none, the mission's own).</summary>
    template <typename Kind> std::optional<Kind> ChosenOf(int32_t value)
    {
        return value == MissionOwn ? std::nullopt : std::optional(static_cast<Kind>(value));
    }

    void WeatherChanged(int32_t value)
    {
        HostChanges([value](MCMatchOptions& options, const MCMultiPlayer&)
                    { options.Weather = ChosenOf<MCWeatherKind>(value); });
    }

    void TimeChanged(int32_t value)
    {
        HostChanges([value](MCMatchOptions& options, const MCMultiPlayer&)
                    { options.TimeOfDay = ChosenOf<MCTimeOfDay>(value); });
    }

    /// <summary>
    /// The host's change of an option played by <typeparamref name="Rule"/>: the rule wanted when the option is ON
    /// (<typeparamref name="OnPlays"/>, as BRAKING) or when it is OFF (as ARTILLERY: the rule bans it).
    /// </summary>
    template <uint32_t Rule, bool OnPlays> void RuleChanged(int32_t value)
    {
        HostChanges(
            [value](MCMatchOptions& options, const MCMultiPlayer& game)
            {
                const bool plays = (value != 0) == OnPlays;
                options.Rules = (game.WantedRules() & ~Rule) | (plays ? Rule : 0);
            });
    }

    void PilotChanged(int32_t value)
    {
        HostChanges(
            [value](MCMatchOptions& options, const MCMultiPlayer& game)
            {
                options.Rules =
                    (game.WantedRules() & ~MCMatchRule::PilotRanks) | MCPilotRankRules(static_cast<MCPilotRank>(value));
            });
    }

    /// <summary>A row's value (1 ON, 0 OFF) for an option played by <paramref name="rule"/> in <paramref name="rules"/>.</summary>
    int OnOf(uint32_t rules, uint32_t rule, bool onPlays)
    {
        return ((rules & rule) != 0) == onPlays ? 1 : 0;
    }

    /// <summary>A row's value from a weather or time of day the match plays (none: <see cref="MissionOwn"/>).</summary>
    template <typename Kind> int ValueOf(const std::optional<Kind>& played)
    {
        return played.has_value() ? static_cast<int>(*played) : MissionOwn;
    }

    /// <summary>A player the session doesn't know, as named in why an option is held.</summary>
    constexpr std::string_view UnknownPlayer = "A PLAYER";

    /// <summary>
    /// Why the host can't choose what <paramref name="rule"/> covers (MatchTellToUpgrade): the other players whose game
    /// doesn't have it, by name (empty: every player's has it).
    /// </summary>
    std::vector<std::string> NamesWithout(MCMultiPlayer& game, uint32_t rule)
    {
        std::vector<std::string> names;

        for (const uint32_t id : game.PlayersWithout(rule))
        {
            const std::shared_ptr<MCFidpPlayer> player = game.SessionManager->GetPlayer(id);
            names.emplace_back(player != nullptr ? std::string_view(player->Name) : UnknownPlayer);
        }

        return names;
    }

    std::string TellToUpgrade(MCMultiPlayer& game, uint32_t rule)
    {
        std::vector<std::string> names = NamesWithout(game, rule);
        std::ranges::transform(names, names.begin(), [](const std::string& name) { return MCUpperText(name); });
        return MatchTellToUpgrade(names);
    }

    /// <summary>Weapon family <typeparamref name="Family"/>'s row (WeaponFamilies): ON sells and fields it, OFF bans it.</summary>
    template <size_t Family> constexpr Row WeaponRow()
    {
        return Row{.Label = WeaponFamilies[Family].Label,
                   .Description = WeaponFamilies[Family].Description,
                   .Value = WeaponValues[Family],
                   .Choices = OffOn,
                   .Changed = RuleChanged<WeaponFamilies[Family].Rule, false>};
    }

    constexpr std::array Rows = {
        Row{.Label = "PLAY"},
        Row{.Label = MCCapabilityOf(MCMatchOption::Weather).Name,
            .Description = "RAIN OR SNOW HAMPERS THE FIGHT.",
            .Value = WeatherValue,
            .Choices = WeatherChoices,
            .Changed = WeatherChanged},
        Row{.Label = MCCapabilityOf(MCMatchOption::TimeOfDay).Name,
            .Description = "DUSK OR NIGHT: UNITS SEE LESS.",
            .Value = TimeValue,
            .Choices = TimeChoices,
            .Changed = TimeChanged},
        Row{.Label = MCCapabilityOf(MCMatchOption::Braking).Name,
            .Description = "MECHS BRAKE TO A HALT.",
            .Value = BrakingValue,
            .Choices = OffOn,
            .Changed = RuleChanged<MCMatchRule::MechBraking, true>},
        Row{.Label = MCCapabilityOf(MCMatchOption::Artillery).Name,
            .Description = "COMMANDERS CALL IN STRIKES.",
            .Value = ArtilleryValue,
            .Choices = OffOn,
            .Changed = RuleChanged<MCMatchRule::NoArtillery, false>},
        Row{.Label = MCCapabilityOf(MCMatchOption::Probes).Name,
            .Description = "SENSOR PROBES AND CAMERA DRONES.",
            .Value = ProbesValue,
            .Choices = OffOn,
            .Changed = RuleChanged<MCMatchRule::NoProbes, false>},
        Row{.Label = MCCapabilityOf(MCMatchOption::Sensors).Name,
            .Description = "UNITS SPOT FOES PAST SIGHT.",
            .Value = SensorsValue,
            .Choices = OffOn,
            .Changed = RuleChanged<MCMatchRule::NoSensors, false>},
        Row{.Label = MCCapabilityOf(MCMatchOption::Mines).Name,
            .Description = "MINEFIELDS, AND MINE LAYERS.",
            .Value = MinesValue,
            .Choices = OffOn,
            .Changed = RuleChanged<MCMatchRule::NoMines, false>},
        Row{.Label = MCCapabilityOf(MCMatchOption::MostSkilledPilot).Name,
            .Description = "NONE MORE SKILLED FOR HIRE.",
            .Value = PilotValue,
            .Choices = PilotChoices,
            .Changed = PilotChanged},
        Row{.Label = MCCapabilityOf(MCMatchOption::Weapons).Name},
        WeaponRow<0>(),
        WeaponRow<1>(),
        WeaponRow<2>(),
        WeaponRow<3>(),
        WeaponRow<4>(),
        WeaponRow<5>(),
        WeaponRow<6>(),
        WeaponRow<7>(),
        WeaponRow<8>(),
        WeaponRow<9>(),
        WeaponRow<10>(),
    };

    static_assert(WeaponFamilies.size() == 11, "a row for each weapon family");

    /// <summary>Each option's row, and the option (what it needs of a game's build: MCOptionCapabilities).</summary>
    struct RuleRow
    {
        int32_t Row = 0;
        MCMatchOption Option = MCMatchOption::Weather;
    };

    /// <summary>The WEAPONS heading's row, its families' rows after it: a heading held holds them all.</summary>
    constexpr int32_t WeaponsRow = 9;
    constexpr std::array RuleRows = {
        RuleRow{1, MCMatchOption::Weather},         RuleRow{2, MCMatchOption::TimeOfDay},
        RuleRow{3, MCMatchOption::Braking},         RuleRow{4, MCMatchOption::Artillery},
        RuleRow{5, MCMatchOption::Probes},          RuleRow{6, MCMatchOption::Sensors},
        RuleRow{7, MCMatchOption::Mines},           RuleRow{8, MCMatchOption::MostSkilledPilot},
        RuleRow{WeaponsRow, MCMatchOption::Weapons}};

    static_assert(RuleRows.size() == MCOptionCapabilities.size(), "Every option of the capability map has its row");

    /// <summary>Whether <paramref name="row"/> edits <paramref name="value"/>.</summary>
    constexpr bool Edits(const Row& row, const int& value)
    {
        return row.Value.has_value() && std::addressof(row.Value->get()) == std::addressof(value);
    }

    static_assert(Edits(Rows[RuleRows[0].Row], WeatherValue) && Edits(Rows[RuleRows[1].Row], TimeValue) &&
                  Edits(Rows[RuleRows[2].Row], BrakingValue) && Edits(Rows[RuleRows[3].Row], ArtilleryValue) &&
                  Edits(Rows[RuleRows[4].Row], ProbesValue) && Edits(Rows[RuleRows[5].Row], SensorsValue) &&
                  Edits(Rows[RuleRows[6].Row], MinesValue) && Edits(Rows[RuleRows[7].Row], PilotValue) &&
                  Rows[WeaponsRow].IsHeading() && Edits(Rows[WeaponsRow + 1], WeaponValues[0]));
}

auto ReadMatchOptions() -> void
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr)
    {
        return;
    }

    // What the match plays.
    const MCMatchOptions& played = game->Played;
    WeatherValue = ValueOf(played.Weather);
    TimeValue = ValueOf(played.TimeOfDay);
    const uint32_t rules = played.Rules.value_or(0);
    BrakingValue = OnOf(rules, MCMatchRule::MechBraking, true);
    ArtilleryValue = OnOf(rules, MCMatchRule::NoArtillery, false);
    ProbesValue = OnOf(rules, MCMatchRule::NoProbes, false);
    SensorsValue = OnOf(rules, MCMatchRule::NoSensors, false);
    MinesValue = OnOf(rules, MCMatchRule::NoMines, false);
    PilotValue = static_cast<int>(MCMostSkilledPilot(rules));

    for (size_t family = 0; family < WeaponFamilies.size(); family++)
    {
        WeaponValues[family] = OnOf(rules, WeaponFamilies[family].Rule, false);
    }
}

auto MatchTellToUpgrade(std::span<const std::string> names) -> std::string
{
    if (names.empty())
    {
        return {};
    }

    return std::format("TELL {} TO UPGRADE.", MCNamesJoined(names));
}

auto MatchOptionRows() -> std::span<const MCOptionRow>
{
    return Rows;
}

auto MatchOptionHeldBy(int32_t row) -> std::vector<std::string>
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    // Out of a session no player is asked (MatchOptionHeld).
    if (game == nullptr || game->SessionManager == nullptr || game->SessionManager->MyPlayerID == 0)
    {
        return {};
    }

    const int32_t asked = row > WeaponsRow ? WeaponsRow : row;
    const auto found = std::ranges::find(RuleRows, asked, &RuleRow::Row);
    return found != RuleRows.end() ? NamesWithout(*game, MCCapabilityOf(found->Option).Rules)
                                   : std::vector<std::string>();
}

auto MatchOptionHeld(int32_t row) -> std::string
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    // Out of a session (the room ending under a view still shown) no player is asked: their list would be made again.
    if (game == nullptr || game->SessionManager == nullptr || game->SessionManager->MyPlayerID == 0)
    {
        return {};
    }

    // A weapon family's row is held with the WEAPONS heading's.
    const int32_t asked = row > WeaponsRow ? WeaponsRow : row;
    const auto found = std::ranges::find(RuleRows, asked, &RuleRow::Row);
    return found != RuleRows.end() ? TellToUpgrade(*game, MCCapabilityOf(found->Option).Rules) : std::string();
}

auto MatchOptionHolds(MCMultiPlayer& game) -> std::vector<MCMatchOptionHold>
{
    std::vector<MCMatchOptionHold> holds;
    holds.reserve(RuleRows.size());

    for (const RuleRow& option : RuleRows)
    {
        holds.push_back({.Row = option.Row, .Why = TellToUpgrade(game, MCCapabilityOf(option.Option).Rules)});
    }

    return holds;
}
