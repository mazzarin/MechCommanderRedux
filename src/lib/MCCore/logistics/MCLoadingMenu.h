#pragma once

// Port-only (SPEC-menus.md, "Into a mission and back"; stage 8, step 1): the loading screen as designed, one RmlUi screen
// (ui/loading.rml) over the main menu's painting: the mission's name and where it is, its objectives and the orders'
// first lines, its map, the load's progress along the foot; nothing to press. MCMission::ShowLoadingScreen shows it in
// the dark after BEGIN MISSION, read from the briefing before logistics goes; the load's frames move its meter
// (gui/MCUpdateDisplay's progress); MCMission::TakeDownLoadingScreen hides it in the dark before the battlefield. Since
// stage 9 every load shows it, 1998's loading picture gone: one with no menus before it (-mission's game segment,
// -benchmark, the staged battle, the tests') shows it for the load alone (MCMission::StartScenario), read from the
// mission's own files.
// In a match the players under the map, each lit once in (the host is told each game's; a guest only knows its own),
// and the match's start waits behind it (MCMission::StartIntoTheBattle).

#include "ui/MCRmlScreen.h"

class MCRmlModel;

/// <summary>The loading screen.</summary>
class MCLoadingScreen final : public MCRmlScreen
{
public:
    /// <summary>A line it shows: an objective with its number (and its own time limit), a paragraph of the orders.</summary>
    struct MCLine
    {
        std::string Text;
        int Number = 0;
        std::string Limit;
    };

    /// <summary>A match's player under the map: the name in their colour, lit once in, and the word for it.</summary>
    struct MCPlayerRow
    {
        std::string Name;
        /// <summary>The name's colour class (the chat's: MCChatView::ColourOf).</summary>
        std::string Colour;
        bool In = false;
        /// <summary>"In", "Loading" (the host's), or none (a guest isn't told the others').</summary>
        std::string Word;

        bool operator==(const MCPlayerRow&) const = default;
    };

    MCLoadingScreen();
    ~MCLoadingScreen() override;

    /// <summary>The screen set up for the mission logistics has now, its meter empty.</summary>
    void SetUp();
    /// <summary>The load's progress, a percentage (the bar's).</summary>
    void SetProgress(int percent);
    /// <summary>This game's load done: in a match it's in, and the foot says whom the match waits for.</summary>
    void SetLoaded();

    /// <summary>What shows (the tests read it).</summary>
    const std::string& Eyebrow() const { return _Eyebrow; }
    const std::string& Title() const { return _Title; }
    /// <summary>The mission's time limit ("15:00"; empty: none), at the right of OBJECTIVES.</summary>
    const std::string& TimeLimit() const { return _TimeLimit; }
    const std::vector<MCLine>& Objectives() const { return _Objectives; }
    const std::vector<MCLine>& Orders() const { return _Orders; }
    const std::string& MapSource() const { return _Map; }
    int Percent() const { return _Percent; }
    const std::string& What() const { return _What; }
    /// <summary>A match's players, and how many are in of them ("1/2 in"; none on a guest, which isn't told).</summary>
    const std::vector<MCPlayerRow>& Players() const { return _Players; }
    const std::string& Count() const { return _Count; }

    void OnFrame() override;
    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;

private:
    /// <summary>A match's player as read at the set-up, while the room's model is there.</summary>
    struct MCWho
    {
        uint32_t Id = 0;
        bool Me = false;
    };

    /// <summary>A match's players' lights, count and the foot read again; whether they changed.</summary>
    bool FillPlayers();

    std::unique_ptr<MCRmlModel> _Model;
    std::string _Eyebrow;
    std::string _Title;
    std::string _TimeLimit;
    std::vector<MCLine> _Objectives;
    std::vector<MCLine> _Orders;
    /// <summary>The map as a decorator (none: no map).</summary>
    std::string _Map;
    std::string _What;
    int _Percent = 0;
    bool _Match = false;
    /// <summary>This game is the match's host, told each game's load (only its PlayerCheckedIn is kept).</summary>
    bool _Host = false;
    bool _Loaded = false;
    std::vector<MCWho> _Who;
    std::vector<MCPlayerRow> _Players;
    std::string _Count;
};

namespace MCLoadingMenu
{
    /// <summary>
    /// The loading screen shown for the mission logistics has now, its meter empty. Port-only (stage 9): where it can't
    /// be read, the game stops (MCRmlFlow::Needed).
    /// </summary>
    void Show();

    /// <summary>The load's progress (a percentage), shown on its meter while it shows.</summary>
    void Progress(int percent);

    /// <summary>The screen hidden (the battlefield comes in).</summary>
    void Hide();

    /// <summary>This game's load done (<see cref="MCLoadingScreen::SetLoaded"/>), while the screen shows.</summary>
    void Loaded();

    /// <summary>Whether the screen shows for a match: its start waits behind it (MCMission::StartIntoTheBattle).</summary>
    bool HoldsForMatch();

    /// <summary>The screen while the menus have it, and whether it shows.</summary>
    MCOptionalRef<MCLoadingScreen> Screen();
    bool Shown();

    /// <summary>The words: over the title, what loads.</summary>
    inline constexpr std::string_view EyebrowFormat = "Operation {} · {} · Mission {}";
    inline constexpr std::string_view LoadingText = "Loading the battlefield";
    /// <summary>A match's: over the title its room (LAN, Internet, a launcher's) and the room's name.</summary>
    inline constexpr std::string_view MatchEyebrowFormat = "{} · {}";
    /// <summary>A match's player in, or still loading (the host's word: it's told each game's).</summary>
    inline constexpr std::string_view InWord = "In";
    inline constexpr std::string_view LoadingWord = "Loading";
    inline constexpr std::string_view CountFormat = "{}/{} in";
    /// <summary>The foot once this game's load is done: the host names whom the match waits for; a guest can't.</summary>
    inline constexpr std::string_view WaitingForFormat = "Loaded. Waiting for {}";
    inline constexpr std::string_view WaitingForOthersText = "Waiting for the others.";
    /// <summary>The orders' first paragraphs it shows (the frames' two).</summary>
    inline constexpr size_t OrdersShown = 2;
}
