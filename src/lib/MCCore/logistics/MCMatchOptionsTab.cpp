#include "stdafx.h"
#include "logistics/MCMatchOptionsTab.h"
#include "logistics/MCLogOptionList.h"
#include "linkup/MCFidpPlayer.h"
#include "logistics/MCLogPort.h"
#include "network/MCMultiPlayer.h"
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

    /// <summary>
    /// The two tabs with the bottom one's word <see cref="MatchOptionsWord"/>, in front (<paramref name="front"/>: as
    /// lsbdw03, MULTIPLAYER CHAT behind) or behind (as lsbdw02); null when the art isn't the tabs' as expected.
    /// </summary>
    std::shared_ptr<MCLogPort> LetterTabs(bool front)
    {
        const std::shared_ptr<MCLogPort> target = LogScreenArt(front ? ChatBehind : ChatInFront);

        if (!IsTabsPicture(*target))
        {
            SDL_Log("MATCH OPTIONS: the tabs' picture is %dx%d, not %dx%d; no tab drawn", target->Width(),
                    target->Height(), TabsWidth, TabsHeight);
            return nullptr;
        }

        auto tabs = std::make_shared<MCLogPort>();
        tabs->Init(TabsWidth, TabsHeight);
        std::ranges::copy(target->Pixels(), tabs->Pixels().begin());
        const std::pair<int32_t, int32_t> face = front ? FrontFace : BehindFace;

        // MISSION BRIEFING wiped with rows of the face left blank above and below it.
        std::vector<int32_t> blankRows;

        for (const auto& [first, last] : BlankFaceRows)
        {
            for (int32_t row = first; row <= last; row++)
            {
                blankRows.push_back(row);
            }
        }

        for (int32_t row = OldWordTop; row <= OldWordBottom; row++)
        {
            CopyFaceRow(*target, blankRows[static_cast<size_t>(row - OldWordTop) % blankRows.size()], *tabs, row, face);
        }

        // The letters in their place, the word centred where the old one was.
        int32_t rows = 0;

        for (const TabLetters& letters : MatchOptionsLetters)
        {
            rows += letters.Rows;
        }

        int32_t row = (OldWordTop + OldWordBottom + 1 - rows) / 2;

        for (const TabLetters& letters : MatchOptionsLetters)
        {
            if (letters.Word.has_value())
            {
                const std::shared_ptr<MCLogPort> source = LogScreenArt(PictureOf(*letters.Word, front));

                if (!IsTabsPicture(*source))
                {
                    SDL_Log("MATCH OPTIONS: %.*s's picture isn't a tabs picture; no tab drawn",
                            static_cast<int>(letters.Letters.size()), letters.Letters.data());
                    return nullptr;
                }

                for (int32_t offset = 0; offset < letters.Rows; offset++)
                {
                    CopyFaceRow(*source, letters.Top + offset, *tabs, row + offset, face);
                }
            }

            row += letters.Rows;
        }

        if (front)
        {
            return tabs;
        }

        // Behind MULTIPLAYER CHAT only the bottom tab is drawn: the background has the one in front.
        auto behind = std::make_shared<MCLogPort>();
        behind->Init(TabsWidth, TabsHeight - BehindTop);
        std::ranges::copy(tabs->Pixels().subspan(static_cast<size_t>(BehindTop * TabsWidth)), behind->Pixels().begin());
        return behind;
    }

    /// <summary>The rows' values: a weather or time of day (<see cref="MissionOwn"/>: the mission's own), braking on.</summary>
    constexpr int32_t MissionOwn = MCMPMatchOptionsMessage::MissionOwn;
    int WeatherValue = MissionOwn;
    int TimeValue = MissionOwn;
    int BrakingValue = 0;

    constexpr std::string_view MissionOwnName = "MISSION'S";
    constexpr std::array WeatherChoices = {Choice{MissionOwnName, MissionOwn},
                                           Choice{"CLEAR", static_cast<int32_t>(MCWeatherKind::Clear)},
                                           Choice{"RAIN", static_cast<int32_t>(MCWeatherKind::Rain)},
                                           Choice{"DOWNPOUR", static_cast<int32_t>(MCWeatherKind::Downpour)},
                                           Choice{"LIGHT SNOW", static_cast<int32_t>(MCWeatherKind::LightSnow)},
                                           Choice{"SNOW", static_cast<int32_t>(MCWeatherKind::Snow)},
                                           Choice{"BLIZZARD", static_cast<int32_t>(MCWeatherKind::Blizzard)}};
    constexpr std::array TimeChoices = {Choice{MissionOwnName, MissionOwn},
                                        Choice{"DAY", static_cast<int32_t>(MCTimeOfDay::Day)},
                                        Choice{"DUSK", static_cast<int32_t>(MCTimeOfDay::Dusk)},
                                        Choice{"NIGHT", static_cast<int32_t>(MCTimeOfDay::Night)}};
    constexpr std::array OffOn = {Choice{"OFF", 0}, Choice{"ON", 1}};

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

    void BrakingChanged(int32_t value)
    {
        HostChanges(
            [value](MCMatchOptions& options, const MCMultiPlayer& game)
            {
                options.Rules =
                    (game.WantedRules() & ~MCMatchRule::MechBraking) | (value != 0 ? MCMatchRule::MechBraking : 0);
            });
    }

    /// <summary>A row's value from a weather or time of day the match plays (none: <see cref="MissionOwn"/>).</summary>
    template <typename Kind> int ValueOf(const std::optional<Kind>& played)
    {
        return played.has_value() ? static_cast<int>(*played) : MissionOwn;
    }

    /// <summary>
    /// Why the host can't choose what <paramref name="rule"/> covers: the players whose game doesn't have it, named when
    /// one (empty: every player's has it).
    /// </summary>
    std::string NotInGames(MCMultiPlayer& game, uint32_t rule)
    {
        const std::vector<uint32_t> without = game.PlayersWithout(rule);

        if (without.empty())
        {
            return {};
        }

        const std::shared_ptr<MCFidpPlayer> player = game.SessionManager->GetPlayer(without.front());

        if (without.size() > 1 || player == nullptr)
        {
            return std::format("NOT IN {} PLAYERS' GAMES.", without.size());
        }

        // Through unsigned char: a player's name may hold any byte, and toupper takes only those and EOF.
        std::string name = player->Name;
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return std::format("NOT IN {}'S GAME.", name);
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

    constexpr std::array Rows = {
        Row{.Label = "LOOK"},
        Row{.Label = "WEATHER",
            .Description = "RAIN OR SNOW FOR EVERYONE.",
            .Value = &WeatherValue,
            .Choices = WeatherChoices,
            .Changed = WeatherChanged},
        Row{.Label = "PLAY"},
        Row{.Label = "TIME OF DAY",
            .Description = "DUSK OR NIGHT: UNITS SEE LESS.",
            .Value = &TimeValue,
            .Choices = TimeChoices,
            .Changed = TimeChanged},
        Row{.Label = "BRAKING",
            .Description = "MECHS BRAKE TO A HALT.",
            .Value = &BrakingValue,
            .Choices = OffOn,
            .Changed = BrakingChanged},
    };

    /// <summary>Each option's row and the rule a player's build must play by for the host to choose it.</summary>
    struct RuleRow
    {
        int32_t Row = 0;
        uint32_t Rule = 0;
    };

    constexpr std::array RuleRows = {RuleRow{1, MCMatchRule::MatchWeather}, RuleRow{3, MCMatchRule::TimeOfDayRules},
                                     RuleRow{4, MCMatchRule::MechBraking}};
    static_assert(Rows[RuleRows[0].Row].Value == &WeatherValue && Rows[RuleRows[1].Row].Value == &TimeValue &&
                  Rows[RuleRows[2].Row].Value == &BrakingValue);
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

auto MakeMatchOptionsList() -> std::shared_ptr<MCLogOptionList>
{
    auto list = MCMakeGui<MCLogOptionList>();
    list->Init(ListLeft, ListTop, ListWidth, ListHeight, Rows);
    list->SetNarrow(SettingWidth);
    list->SetSoftwareHint({});
    list->SetHeadingColor(HeadingColor);
    list->ShowGuiWindow(false);
    return list;
}

auto ShowMatchOptions(MCLogOptionList& list) -> void
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr)
    {
        return;
    }

    // What the match plays; on the host, an option a player's game doesn't have held at the mission's own (or off),
    // saying whose.
    const MCMatchOptions& played = game->Played;
    WeatherValue = ValueOf(played.Weather);
    TimeValue = ValueOf(played.TimeOfDay);
    BrakingValue = (played.Rules.value_or(0) & MCMatchRule::MechBraking) != 0 ? 1 : 0;
    const bool host = game->IsHost != 0;
    list.SetLocked(!host, LockedHint);

    for (const RuleRow& option : RuleRows)
    {
        list.SetHeld(option.Row, host ? NotInGames(*game, option.Rule) : std::string());
    }
}
