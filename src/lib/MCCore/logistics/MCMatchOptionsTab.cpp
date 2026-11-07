#include "stdafx.h"
#include "logistics/MCMatchOptionsTab.h"
#include "logistics/MCLogOptionList.h"
#include "linkup/MCFidpPlayer.h"
#include "main/MCWeaponBans.h"
#include "logistics/MCLogPort.h"
#include "network/MCMultiPlayer.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    using Choice = MCLogOptionList::Choice;
    using Row = MCLogOptionList::Row;

    /// <summary>
    /// The briefing's tab pictures (13 x 231, two tabs a picture, the top one's word read down it): lsbdw00 OPERATION
    /// BRIEFING in front of MISSION BRIEFING, lsbdw01 behind it; lsbdw02 MULTIPLAYER CHAT in front of MISSION BRIEFING,
    /// lsbdw03 behind it; lsbdw08 MULTIPLAYER CHAT's word lit white (its blink).
    /// </summary>
    constexpr std::string_view OperationInFront = "lsbdw00.tga";
    constexpr std::string_view OperationBehind = "lsbdw01.tga";
    constexpr std::string_view ChatInFront = "lsbdw02.tga";
    constexpr std::string_view ChatBehind = "lsbdw03.tga";
    constexpr std::string_view ChatBlink = "lsbdw08.tga";
    constexpr int32_t TabsWidth = 13;
    constexpr int32_t TabsHeight = 231;

    /// <summary>
    /// Where the session screen's background has MULTIPLAYER CHAT as lsbdw02's top tab, and the first of the pictures'
    /// rows that the tab behind it shows of (the rows above are the front tab's).
    /// </summary>
    constexpr int32_t TabsLeft = 0xc4;
    constexpr int32_t TabsTop = 0x41;
    constexpr int32_t BehindTop = 122;
    /// <summary>MULTIPLAYER CHAT's blink is drawn a column right of the tabs (as the briefing draws it).</summary>
    constexpr int32_t BlinkLeft = TabsLeft + 1;

    /// <summary>Where a click picks each tab, on the session screen (inclusive), as the briefing's tabs are picked.</summary>
    constexpr RECT ChatTabArea = {0xc2, 0x43, 0xd1, TabsTop + BehindTop - 1};
    constexpr RECT OptionsTabArea = {0xc2, TabsTop + BehindTop, 0xd1, TabsTop + TabsHeight - 1};

    /// <summary>
    /// A tab's face, the columns its word is written on (inclusive): a tab in front reaches the panel at its left, one
    /// behind is drawn in a column. The rows of the bottom tab's word (MISSION BRIEFING), and rows of its face with no
    /// word above and below it.
    /// </summary>
    constexpr std::pair<int32_t, int32_t> FrontFace = {0, 10};
    constexpr std::pair<int32_t, int32_t> BehindFace = {2, 11};
    constexpr int32_t OldWordTop = 133;
    constexpr int32_t OldWordBottom = 216;
    constexpr std::array<std::pair<int32_t, int32_t>, 2> BlankFaceRows = {{{123, 132}, {217, 225}}};

    /// <summary>A tab's word in the pictures: on the top tab, MULTIPLAYER CHAT or OPERATION BRIEFING; on the bottom one, MISSION BRIEFING.</summary>
    enum class TabWord : uint8_t
    {
        Chat,
        Operation,
        Mission,
    };

    /// <summary>
    /// A run of <see cref="MatchOptionsWord"/>'s letters as a tab's word has them, one after another (each letter's
    /// rows touch its neighbours'): the word and its rows; none, rows of blank face (a space, or the gap a letter
    /// needs that its word had beside it).
    /// </summary>
    struct TabLetters
    {
        std::string_view Letters;
        std::optional<TabWord> Word;
        int32_t Top = 0;
        int32_t Rows = 0;
    };

    /// <summary>MATCH OPTIONS: M, AT and CH from MULTIPLAYER CHAT, OP and TION from OPERATION, S from MISSION.</summary>
    constexpr std::array MatchOptionsLetters = {
        TabLetters{"M", TabWord::Chat, 18, 8},        TabLetters{"AT", TabWord::Chat, 94, 11},
        TabLetters{"CH", TabWord::Chat, 82, 12},      TabLetters{" ", std::nullopt, 0, 3},
        TabLetters{"OP", TabWord::Operation, 13, 12}, TabLetters{"TION", TabWord::Operation, 43, 20},
        TabLetters{"", std::nullopt, 0, 1},           TabLetters{"S", TabWord::Mission, 145, 5},
    };

    /// <summary>The picture with <paramref name="word"/> on the tab in front (<paramref name="front"/>) or behind.</summary>
    std::string_view PictureOf(TabWord word, bool front)
    {
        switch (word)
        {
            case TabWord::Chat:
                return front ? ChatInFront : ChatBehind;
            case TabWord::Operation:
                return front ? OperationInFront : OperationBehind;
            case TabWord::Mission:
            default:
                // MISSION BRIEFING is the bottom tab: in front where the top one is behind.
                return front ? OperationBehind : OperationInFront;
        }
    }

    /// <summary>Whether <paramref name="art"/> is a tabs picture's size.</summary>
    bool IsTabsPicture(const MCLogPort& art)
    {
        return art.Width() == TabsWidth && art.Height() == TabsHeight;
    }

    /// <summary>
    /// Copies row <paramref name="from"/>'s face (<paramref name="face"/>'s columns) of <paramref name="source"/> to row
    /// <paramref name="to"/> of <paramref name="target"/>.
    /// </summary>
    void CopyFaceRow(const MCLogPort& source, int32_t from, MCLogPort& target, int32_t to,
                     std::pair<int32_t, int32_t> face)
    {
        const std::span<const uint8_t> sourceRow = source.Pixels().subspan(static_cast<size_t>(from * TabsWidth));
        const std::span<uint8_t> targetRow = target.Pixels().subspan(static_cast<size_t>(to * TabsWidth));
        std::copy(sourceRow.begin() + face.first, sourceRow.begin() + face.second + 1, targetRow.begin() + face.first);
    }

    /// <summary>A row of a tab's face put into the lettered tabs: row <c>From</c> of a picture as row <c>To</c>.</summary>
    struct FaceRow
    {
        std::string_view Picture;
        int32_t From = 0;
        int32_t To = 0;
    };

    /// <summary>
    /// The picture the lettered tabs are made on, with MATCH OPTIONS in front (<paramref name="front"/>: lsbdw03,
    /// MULTIPLAYER CHAT behind) or behind (lsbdw02).
    /// </summary>
    std::string_view TabsPictureFor(bool front)
    {
        return front ? ChatBehind : ChatInFront;
    }

    /// <summary>
    /// The face rows that letter <see cref="MatchOptionsWord"/> on <see cref="TabsPictureFor"/>'s picture: MISSION
    /// BRIEFING wiped with rows of the face left blank above and below it, then the letters, the word centred where
    /// the old one was.
    /// </summary>
    std::vector<FaceRow> LetteredRows(bool front)
    {
        const std::string_view tabs = TabsPictureFor(front);
        std::vector<int32_t> blankRows;

        for (const auto& [first, last] : BlankFaceRows)
        {
            for (int32_t row = first; row <= last; row++)
            {
                blankRows.push_back(row);
            }
        }

        std::vector<FaceRow> rows;

        for (int32_t row = OldWordTop; row <= OldWordBottom; row++)
        {
            rows.push_back({tabs, blankRows[static_cast<size_t>(row - OldWordTop) % blankRows.size()], row});
        }

        int32_t letterRows = 0;

        for (const TabLetters& letters : MatchOptionsLetters)
        {
            letterRows += letters.Rows;
        }

        int32_t row = (OldWordTop + OldWordBottom + 1 - letterRows) / 2;

        for (const TabLetters& letters : MatchOptionsLetters)
        {
            if (letters.Word.has_value())
            {
                for (int32_t offset = 0; offset < letters.Rows; offset++)
                {
                    rows.push_back({PictureOf(*letters.Word, front), letters.Top + offset, row + offset});
                }
            }

            row += letters.Rows;
        }

        return rows;
    }

    /// <summary>The pictures the lettered tabs are made of, by name.</summary>
    using TabsArt = std::map<std::string_view, std::shared_ptr<MCLogPort>>;

    /// <summary>
    /// <paramref name="tabs"/>' picture and those <paramref name="rows"/> are from; none (logged) when one isn't a tabs
    /// picture.
    /// </summary>
    std::optional<TabsArt> ArtOf(std::string_view tabs, std::span<const FaceRow> rows)
    {
        TabsArt art{{tabs, nullptr}};

        for (const FaceRow& row : rows)
        {
            art.emplace(row.Picture, nullptr);
        }

        for (auto& [name, picture] : art)
        {
            picture = LogScreenArt(name);

            if (!IsTabsPicture(*picture))
            {
                SDL_Log("MATCH OPTIONS: %.*s is %dx%d, not %dx%d as the tabs' pictures are; no tab drawn",
                        static_cast<int>(name.size()), name.data(), picture->Width(), picture->Height(), TabsWidth,
                        TabsHeight);
                return std::nullopt;
            }
        }

        return art;
    }

    /// <summary>
    /// Port-only: gives the lettered tabs (<paramref name="tabs"/>, the rows from <paramref name="top"/> down of
    /// <paramref name="base"/>'s picture with <paramref name="rows"/>' faces put in) an HD copy, made as they are of
    /// the HD art pack's copies of their pictures, for the pack's pictures to show (MCHdArt::KeepPicture). None while HD
    /// art is off, or when the pack lacks one of the pictures: the tabs show as they are.
    /// </summary>
    void KeepHdTabs(const MCLogPort& tabs, int32_t top, const TabsArt& art, std::string_view base,
                    std::span<const FaceRow> rows, std::pair<int32_t, int32_t> face)
    {
        std::map<std::string_view, std::shared_ptr<const MCHdArt::Frame>> hd;

        for (const auto& [name, picture] : art)
        {
            std::shared_ptr<const MCHdArt::Frame> frame =
                MCHdArt::FindPicture(picture->Pixels(), picture->Width(), picture->Height());

            if (frame == nullptr)
            {
                return;
            }

            hd.emplace(name, std::move(frame));
        }

        const MCHdArt::Frame& made = *hd.at(base);
        MCHdArt::Frame lettered = made;
        const int32_t faceWidth = face.second - face.first + 1;
        const auto copied = [&](const MCHdArt::Frame& from, int32_t x, int32_t y, int32_t width, int32_t height,
                                MCHdArt::Frame& to, int32_t toX, int32_t toY)
        {
            if (MCHdArt::CopyArea(from, x, y, width, height, to, toX, toY))
            {
                return true;
            }

            SDL_Log(
                "MATCH OPTIONS: the HD art pack's tabs at %dx (format %d) can't be lettered from %dx (format %d); the "
                "tab shows as it is",
                to.Scale, static_cast<int>(to.Format), from.Scale, static_cast<int>(from.Format));
            return false;
        };

        for (const FaceRow& row : rows)
        {
            if (!copied(*hd.at(row.Picture), face.first, row.From, faceWidth, 1, lettered, face.first, row.To))
            {
                return;
            }
        }

        MCHdArt::Frame shown = MCHdArt::BlankFrame(made, TabsWidth, TabsHeight - top);

        if (!copied(lettered, 0, top, TabsWidth, TabsHeight - top, shown, 0, 0))
        {
            return;
        }

        MCHdArt::KeepPicture(tabs.Pixels(), tabs.Width(), tabs.Height(), std::move(shown));
    }

    /// <summary>
    /// The two tabs with the bottom one's word <see cref="MatchOptionsWord"/>, in front (<paramref name="front"/>: as
    /// lsbdw03, MULTIPLAYER CHAT behind) or behind (as lsbdw02), and their HD copy (KeepHdTabs); null when the art isn't
    /// the tabs' as expected.
    /// </summary>
    std::shared_ptr<MCLogPort> LetterTabs(bool front)
    {
        const std::string_view base = TabsPictureFor(front);
        const std::vector<FaceRow> rows = LetteredRows(front);
        const std::optional<TabsArt> art = ArtOf(base, rows);

        if (!art.has_value())
        {
            return nullptr;
        }

        const std::pair<int32_t, int32_t> face = front ? FrontFace : BehindFace;
        MCLogPort lettered;
        lettered.Init(TabsWidth, TabsHeight);
        std::ranges::copy(art->at(base)->Pixels(), lettered.Pixels().begin());

        for (const FaceRow& row : rows)
        {
            CopyFaceRow(*art->at(row.Picture), row.From, lettered, row.To, face);
        }

        // Behind MULTIPLAYER CHAT only the bottom tab is drawn: the background has the one in front.
        const int32_t top = front ? 0 : BehindTop;
        auto tabs = std::make_shared<MCLogPort>();
        tabs->Init(TabsWidth, TabsHeight - top);
        std::ranges::copy(lettered.Pixels().subspan(static_cast<size_t>(top * TabsWidth)), tabs->Pixels().begin());
        KeepHdTabs(*tabs, top, *art, base, rows, face);
        return tabs;
    }

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
    std::string TellToUpgrade(MCMultiPlayer& game, uint32_t rule)
    {
        std::vector<std::string> names;

        for (const uint32_t id : game.PlayersWithout(rule))
        {
            const std::shared_ptr<MCFidpPlayer> player = game.SessionManager->GetPlayer(id);
            names.push_back(MCUpperText(player != nullptr ? std::string_view(player->Name) : UnknownPlayer));
        }

        return MatchTellToUpgrade(names);
    }

    /// <summary>Width of the setting boxes: LIGHT SNOW's in the list's font, with its margins.</summary>
    constexpr int32_t SettingWidth = 64;

    /// <summary>The list's place and size: the chat window's on the session screen (MCSessionScreen::Activate).</summary>
    constexpr int32_t ListLeft = 2;
    constexpr int32_t ListTop = 0x42;
    constexpr int32_t ListWidth = 0xbf;
    constexpr int32_t ListHeight = 0x10a;

    /// <summary>What a guest's list says on its status line.</summary>
    constexpr std::string_view LockedHint = "THE HOST SETS THESE.";
    /// <summary>The headings' amber in the session screen's palette (the preferences' is dark there).</summary>
    constexpr uint8_t HeadingColor = 0xac;

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

auto MakeMatchTabs(MCSessionTab front) -> std::shared_ptr<MCLogPort>
{
    return LetterTabs(front == MCSessionTab::Options);
}

auto DrawMatchTabs(const MCPane& target, const MCLogPort& tabs, MCSessionTab front, bool chatBlinkOn) -> void
{
    if (front == MCSessionTab::Chat)
    {
        tabs.CopyTo(target, TabsLeft, TabsTop + BehindTop, false);
        return;
    }

    tabs.CopyTo(target, TabsLeft, TabsTop, false);

    if (chatBlinkOn)
    {
        LogScreenArt(ChatBlink)->CopyTo(target, BlinkLeft, TabsTop, true);
    }
}

auto MatchTabAt(int32_t xPos, int32_t yPos) -> std::optional<MCSessionTab>
{
    const auto inside = [xPos, yPos](const RECT& area)
    { return xPos >= area.left && xPos <= area.right && yPos >= area.top && yPos <= area.bottom; };

    if (inside(ChatTabArea))
    {
        return MCSessionTab::Chat;
    }

    if (inside(OptionsTabArea))
    {
        return MCSessionTab::Options;
    }

    return std::nullopt;
}

auto AddMatchOptionsList(MCGuiObject& screen) -> MCGuiHandle<MCLogOptionList>
{
    return screen.CreateChild<MCLogOptionList>(
        [](MCLogOptionList& list)
        {
            list.Init(ListLeft, ListTop, ListWidth, ListHeight, Rows);
            list.SetNarrow(SettingWidth);
            list.SetRendererHints(false);
            list.SetHeadingColor(HeadingColor);
            list.ShowGuiWindow(false);
        });
}

auto ShowMatchOptions(MCLogOptionList& list) -> void
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr)
    {
        return;
    }

    // What the match plays; an option a player's game doesn't have held at the mission's own (or off), naming whose.
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

    const bool host = game->IsHost != 0;
    list.SetLocked(!host, LockedHint);

    // An option is held while a player's game plays none of its values (it hasn't the option's first version); a value
    // the room plays at no version is skipped (its choice's RoomPlays).
    for (const RuleRow& option : RuleRows)
    {
        list.SetHeld(option.Row, TellToUpgrade(*game, MCCapabilityOf(option.Option).Rules));
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
