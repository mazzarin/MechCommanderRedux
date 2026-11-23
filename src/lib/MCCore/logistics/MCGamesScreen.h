#pragma once

// Port-only (SPEC-menus.md's stage 3, "LAN and INTERNET"): the remade games screens, the games list's views
// (network/MCGamesList): INTERNET (the service's public games, the wider one players asked for) and LAN (this network's),
// each a list with columns, the picked game's players and lines beside it, PLAYER NAME (and on INTERNET, CODE) and
// HOST GAME and JOIN GAME along the foot. HOST GAME opens a window over the list. Joining an internet game is a job
// with INTERNET live ("Joining Steel Rain..." with CANCEL); its refusals are dialogs over it. They hand over to the
// 1998 screens through the flow: MULTIPLAYER ACCESS under them, the room over them, the UPDATE page for a newer game.

#include "gui/MCScreenFlow.h"
#include "network/MCGamesList.h"
#include "ui/MCRmlScreen.h"

class MCRmlModel;

/// <summary>The change of screen a join or a host runs in while it connects (the tests wait on it).</summary>
inline constexpr std::string_view JoiningChange = "joining";

/// <summary>HOST GAME's window over the games list: the game's name, how many players, who can join.</summary>
class MCHostWindow final : public MCRmlScreen
{
public:
    /// <summary>What the window's HOST GAME asks for.</summary>
    struct MCHost
    {
        std::string GameName;
        int32_t MaxPlayers = 6;
        bool Public = true;
    };

    explicit MCHostWindow(MCGamesWhere where);

    /// <summary>Opens over the list, its name the game offers (<paramref name="gameName"/>), the rest as last set.</summary>
    void OpenWith(std::string gameName);

    /// <summary>What HOST GAME does with the window's choices.</summary>
    std::function<void(const MCHost& host)> Hosting;

    /// <summary>The window's choices as they stand (the tests set them as a player would).</summary>
    MCHost Choices() const;
    void SetChoices(const MCHost& host);

    /// <summary>HOST GAME, as a click would.</summary>
    void PressHost();

    /// <summary>The fewest and the most players a game takes (a game rule).</summary>
    static constexpr int32_t FewestPlayers = 2;
    static constexpr int32_t MostPlayers = 6;

protected:
    void OnLoaded(Rml::ElementDocument& document) override;

private:
    MCGamesWhere _Where;
};

/// <summary>INTERNET or LAN (<c>ui/internet.rml</c>, <c>ui/lan.rml</c>), the games list's view.</summary>
class MCGamesScreen final : public MCRmlScreen
{
public:
    explicit MCGamesScreen(MCGamesWhere where);
    ~MCGamesScreen() override;

    MCGamesWhere Where() const { return _Where; }
    /// <summary>Its model: the games, and the game being joined or hosted.</summary>
    MCGamesList& List() { return _List; }
    MCGameJoin& Join() { return _Join; }

    /// <summary>For the tests: what shows, as a player reads it.</summary>
    struct MCSeen
    {
        /// <summary>The header's line: "58 online, 15 in games", "3 games on this network", "Offline".</summary>
        std::string Counts;
        /// <summary>The list's line when it has no rows ("No open games."), empty with rows.</summary>
        std::string Empty;
        bool Retry = false;
        bool Update = false;
        /// <summary>Each row's columns joined by " | ": "Steel Rain | 2/6 | Europe | Open".</summary>
        std::vector<std::string> Rows;
        int32_t Picked = -1;
        std::string PickName;
        std::vector<std::string> PickLines;
        std::vector<std::string> PickPlayers;
        bool CanJoin = false;
        bool CanHost = false;
    };

    MCSeen Seen() const;

    /// <summary>The player's name and the code typed, as a player would type them.</summary>
    void SetPlayerName(std::string name);
    void SetCode(std::string code);
    const std::string& PlayerName() const { return _PlayerName; }

    /// <summary>Picks row <paramref name="row"/> (a click on it), or the game with <paramref name="key"/>.</summary>
    void PickRow(int32_t row);
    void PickKey(std::string_view key);

    /// <summary>The buttons, as a click would press them.</summary>
    void PressJoin();
    void PressHost();
    void PressRetry();
    void PressUpdate();

    /// <summary>HOST GAME's window (none before HOST GAME first opened it).</summary>
    MCGuiHandle<MCHostWindow> HostWindow() const { return _HostWindow; }

    /// <summary>Hosts a game as <paramref name="host"/> says (HOST GAME's window's HOST GAME).</summary>
    void Host(const MCHostWindow::MCHost& host);

    void OnFrame() override;
    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnShow() override;
    void OnHide() override;
    MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document) override;

private:
    /// <summary>A row as the document binds it.</summary>
    struct MCRow
    {
        std::string Name;
        std::string Players;
        std::string Where;
        std::string Now;
        bool Off = false;
        bool Needs = false;

        bool operator==(const MCRow&) const = default;
    };

    /// <summary>A line of the picked game's.</summary>
    struct MCLine
    {
        std::string Text;

        bool operator==(const MCLine&) const = default;
    };

    /// <summary>A player of the picked game's (a type of its own: the model binds one list to each row type).</summary>
    struct MCPlayer
    {
        std::string Text;

        bool operator==(const MCPlayer&) const = default;
    };

    /// <summary>The list as the model last heard it, copied into what the document binds.</summary>
    void ShowList();
    /// <summary>The picked game's players and lines.</summary>
    void ShowPick();
    /// <summary>JOIN GAME and HOST GAME on or off as the pick, the code and the list stand.</summary>
    void ShowButtons();
    /// <summary>The code typed, upper-cased, when it's a code's length.</summary>
    std::optional<std::string> TypedCode() const;
    /// <summary>The picked game, while listed.</summary>
    std::optional<MCListedGame> Picked() const;
    /// <summary>The place's name, what the screen's dialogs belong to ("Internet", "LAN").</summary>
    std::string Place() const;

    /// <summary>Begins joining or hosting as <paramref name="ask"/> says, its wait shown or its end.</summary>
    void Begin(MCGameJoin::MCAsk ask);
    /// <summary>The join's or the host's end: its room opened, or its refusal said over the list.</summary>
    void Ended(MCGameJoin::MCEnd end);
    /// <summary>Its refusal said, with RETRY or UPDATE when they can help.</summary>
    void SayRefusal();
    /// <summary>The focus to the first game, once the first rows have come and are laid out.</summary>
    void FocusFirstRow();
    /// <summary>A newer game's dialog: "Needs version 0.7.2" over "You have 0.7.0.", CANCEL and UPDATE.</summary>
    void SayNeedsNewer(const MCListedGame& game);
    /// <summary>The UPDATE page (the 1998 one until stage 6), a newer game joined after the restart if one is given.</summary>
    void OpenUpdate(std::optional<MCListedGame> joinAfter);
    /// <summary>CANCEL on the wait: the join called off, the list staying up.</summary>
    void CallOff();

    MCGamesWhere _Where;
    MCGamesList _List;
    MCGameJoin _Join;
    /// <summary>The join's room set up; its change fades the list out and shows it.</summary>
    bool _Entered = false;
    /// <summary>The list let go while the player is away (logistics/MCIdleCheck.h), opened again at the next input.</summary>
    bool _Away = false;
    /// <summary>The first rows have come: the focus moves to the first game once they're laid out.</summary>
    bool _FocusFirstRow = false;
    std::optional<std::string> _PickedKey;
    MCGuiHandle<MCHostWindow> _HostWindow;
    MCHostWindow::MCHost _LastHost;

    // What the document binds (the model's values at its top level).
    std::unique_ptr<MCRmlModel> _Model;
    std::vector<MCRow> _Rows;
    std::string _Counts;
    std::string _Empty;
    bool _Retry = false;
    bool _Update = false;
    int _PickedRow = -1;
    std::string _PickName;
    std::string _PickCount;
    std::vector<MCLine> _PickLines;
    std::vector<MCPlayer> _PickPlayers;
    std::string _PlayerName;
    std::string _Code;
    bool _CanJoin = false;
    bool _CanHost = true;
};

/// <summary>INTERNET's and LAN's kinds in the screen flow (logistics/MCMenuScreens registers them).</summary>
MCScreenFlow::MCScreenKind MCGamesKind(MCGamesWhere where);

/// <summary>The games screen made (none before it first opened).</summary>
MCGuiHandle<MCGamesScreen> MCGamesScreenOf(MCGamesWhere where);

/// <summary>
/// The games screen shown at once, the screen logistics shows hidden (a change's dark work, as the ways back from a room,
/// a match or an update have it); the stack follows at the change's end.
/// </summary>
void ShowGamesNow(MCGamesWhere where);

/// <summary>LAN shown at once over TCP/IP (the network chosen, back from a room or a match), as INTERNET is.</summary>
void ShowLanScreen();

/// <summary>The game quitting: the screens' lists let go (their sockets closed) before RmlUi stops.</summary>
void ReleaseGamesScreens();
