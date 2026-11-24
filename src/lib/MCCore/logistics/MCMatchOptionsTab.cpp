#include "stdafx.h"
#include "logistics/MCMatchOptionsTab.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCMatchOptionRows.h"
#include "network/MCMultiPlayer.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
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
    /// The two tabs with the bottom one's word <see cref="MatchOptionsWord"/>, in front (<paramref name="front"/>: as
    /// lsbdw03, MULTIPLAYER CHAT behind) or behind (as lsbdw02); null when the art isn't the tabs' as expected. Their HD
    /// copy is the HD art pack's, found by their pixels as any picture's is (the pack draws the words in its font, from
    /// the tabs mc_tests' --hd-extract-menus writes).
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
        return tabs;
    }

    /// <summary>Width of the setting boxes: LIGHT SNOW's in the list's font, with its margins.</summary>
    constexpr int32_t SettingWidth = 64;

    /// <summary>The list's place and size: the chat window's on the session screen (MCSessionScreen::Opened).</summary>
    constexpr int32_t ListLeft = 2;
    constexpr int32_t ListTop = 0x42;
    constexpr int32_t ListWidth = 0xbf;
    constexpr int32_t ListHeight = 0x10a;

    /// <summary>What a guest's list says on its status line.</summary>
    constexpr std::string_view LockedHint = "THE HOST SETS THESE.";
    /// <summary>The headings' amber in the session screen's palette (the preferences' is dark there).</summary>
    constexpr uint8_t HeadingColor = 0xac;
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
            list.Init(ListLeft, ListTop, ListWidth, ListHeight, MatchOptionRows());
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
    ReadMatchOptions();
    const bool host = game->IsHost != 0;
    list.SetLocked(!host, LockedHint);

    // An option is held while a player's game plays none of its values (it hasn't the option's first version); a value
    // the room plays at no version is skipped (its choice's RoomPlays).
    for (const MCMatchOptionHold& hold : MatchOptionHolds(*game))
    {
        list.SetHeld(hold.Row, hold.Why);
    }
}
