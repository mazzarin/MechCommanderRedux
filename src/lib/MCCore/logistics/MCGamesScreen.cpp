#include "stdafx.h"
#include "logistics/MCGamesScreen.h"
#include "gui/MCScreenChange.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCIdleCheck.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMultiPlayer.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
    constexpr const char* InternetDocument = "internet.rml";
    constexpr const char* LanDocument = "lan.rml";
    constexpr const char* HostDocument = "host_game.rml";
    constexpr const char* InternetName = "internet";
    constexpr const char* LanName = "lan";
    constexpr const char* InternetPlace = "Internet";
    constexpr const char* LanPlace = "LAN";

    constexpr std::string_view CountsFormat = "{} online, {} in games";
    constexpr std::string_view OfflineCounts = "Offline";
    constexpr std::string_view LanCountFormat = "{} games on this network";
    constexpr std::string_view LanOneCount = "1 game on this network";
    constexpr std::string_view LookingText = "Looking for games...";
    constexpr std::string_view OfflineText = "No internet connection.";
    constexpr std::string_view NotAnsweringText = "The games list isn't answering.";
    constexpr std::string_view UpdateText = "Update to play online.";
    constexpr std::string_view BlockedText = "This copy is blocked from online games.";
    constexpr std::string_view UnavailableText = "This version can't play online.";
    constexpr std::string_view NoGamesText = "No open games.";
    constexpr std::string_view NoLanGamesText = "No games on this network.";
    constexpr std::string_view PublicLine = "Public";
    constexpr std::string_view PublicHostedFormat = "Public, hosted in {}";
    constexpr std::string_view LanLine = "On this network";
    constexpr std::string_view OnMissionFormat = "{}, on {}";
    constexpr std::string_view NeedsFormat = "Needs version {}";
    constexpr std::string_view YouHaveFormat = "You have {}.";
    constexpr std::string_view GameNameFormat = "{}'s game";
    /// <summary>A player with no name remembered, and a game with no name typed (the LAN screen's).</summary>
    constexpr std::string_view NoPlayerName = "Player";
    constexpr std::string_view NoGameName = "Game";

    /// <summary>RmlUi's pseudo-class for the focus's ring (set by MCRmlLayer on a key, cleared by the mouse).</summary>
    constexpr const char* FocusVisible = "focus-visible";
    constexpr std::string_view OkWord = "OK";
    constexpr std::string_view RetryWord = "Retry";
    constexpr std::string_view UpdateWord = "Update";
    constexpr std::string_view CancelWord = "Cancel";

    /// <summary>The service's games' codes' length.</summary>
    constexpr size_t CodeLength = 4;
    /// <summary>The look's class for a list's row.</summary>
    constexpr const char* RowClass = "list-row";
    /// <summary>The games list's scrolled element, holding its rows.</summary>
    constexpr const char* ListId = "list";
    /// <summary>RETRY's button, under the list's line while it's offline or not answering.</summary>
    constexpr const char* RetryId = "retry";
    /// <summary>The look's class for a button that's off.</summary>
    constexpr const char* OffClass = "off";
    constexpr const char* ValueAttribute = "value";
    constexpr const char* Display = "display";
    constexpr const char* NoDisplay = "none";

    /// <summary>A text box's value (RmlUi keeps it as its value attribute).</summary>
    std::string ValueOf(const Rml::Element& element)
    {
        return element.GetAttribute<Rml::String>(ValueAttribute, "");
    }

    std::string Upper(std::string_view text)
    {
        std::string upper(text);
        std::ranges::transform(upper, upper.begin(),
                               [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
        return upper;
    }

    /// <summary>The player's name for the games screens: the one remembered, else "Player".</summary>
    std::string RememberedName()
    {
        return MyGetUserName().value_or(std::string(NoPlayerName));
    }

    /// <summary>Whether the element is a button that's off (its click goes no further).</summary>
    bool IsOff(const MCOptionalRef<Rml::ElementDocument>& document, const char* id)
    {
        Rml::Element* element = document.has_value() ? document->get().GetElementById(id) : nullptr;
        return element != nullptr && element->IsClassSet(OffClass);
    }

    /// <summary>Sets <paramref name="value"/> to <paramref name="now"/>; whether it changed.</summary>
    template <typename T> bool Keep(T& value, T now)
    {
        if (value == now)
        {
            return false;
        }

        value = std::move(now);
        return true;
    }

    const char* NameOf(MCGamesWhere where)
    {
        return where == MCGamesWhere::Internet ? InternetName : LanName;
    }

    MCScreenFlow::MCScreenId IdOf(MCGamesWhere where)
    {
        return where == MCGamesWhere::Internet ? MCMenuScreens::Internet() : MCMenuScreens::Lan();
    }
}

// ---- HOST GAME's window ----

MCHostWindow::MCHostWindow(MCGamesWhere where) : _Where(where)
{
    SetDocument(HostDocument);
    Modal = true;
    Keys = {{"ENTER", "Host game"}, {"ESC", "Cancel"}};
    Back = [this] { Hide(); };
}

void MCHostWindow::OnLoaded(Rml::ElementDocument& document)
{
    OnClick("cancel", [this] { Back(); });
    OnClick("host", [this] { PressHost(); });

    if (Rml::Element* where = document.GetElementById("where"); where != nullptr)
    {
        where->SetInnerRML(_Where == MCGamesWhere::Internet ? InternetPlace : LanPlace);
    }

    // A LAN game is this network's: no one else can find it, so there's no one to keep out.
    if (Rml::Element* who = document.GetElementById("who-row"); who != nullptr && _Where == MCGamesWhere::Lan)
    {
        who->SetProperty(Display, NoDisplay);
    }
}

void MCHostWindow::OpenWith(std::string gameName)
{
    const MCHost last = Document().has_value() ? Choices() : MCHost{};

    if (!Document().has_value())
    {
        // Port-only (stage 9): no 1998 host screen in its place.
        if (const auto opened = Open(); !opened)
        {
            MCRmlFlow::Broken(std::format("HOST GAME's window can't open: {}", opened.error()));
        }
    }
    else
    {
        Show();
    }

    SetChoices(MCHost{.GameName = std::move(gameName), .MaxPlayers = last.MaxPlayers, .Public = last.Public});
}

MCHostWindow::MCHost MCHostWindow::Choices() const
{
    MCHost host;
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value())
    {
        return host;
    }

    if (Rml::Element* name = document->get().GetElementById("game-name"); name != nullptr)
    {
        host.GameName = ValueOf(*name);
    }

    if (Rml::Element* players = document->get().GetElementById("players"); players != nullptr)
    {
        host.MaxPlayers = std::clamp(FewestPlayers + MCRmlKit::Choice(*players), FewestPlayers, MostPlayers);
    }

    if (Rml::Element* who = document->get().GetElementById("who"); who != nullptr && _Where == MCGamesWhere::Internet)
    {
        host.Public = MCRmlKit::Choice(*who) == 0;
    }

    return host;
}

void MCHostWindow::SetChoices(const MCHost& host)
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value())
    {
        return;
    }

    if (Rml::Element* name = document->get().GetElementById("game-name"); name != nullptr)
    {
        name->SetAttribute(ValueAttribute, host.GameName);
    }

    if (Rml::Element* players = document->get().GetElementById("players"); players != nullptr)
    {
        MCRmlKit::SetChoice(*players, std::clamp(host.MaxPlayers, FewestPlayers, MostPlayers) - FewestPlayers);
    }

    if (Rml::Element* who = document->get().GetElementById("who"); who != nullptr)
    {
        MCRmlKit::SetChoice(*who, host.Public ? 0 : 1);
    }
}

void MCHostWindow::PressHost()
{
    MCHost host = Choices();

    if (host.GameName.empty())
    {
        host.GameName = NoGameName;
    }

    if (Hosting)
    {
        Hosting(host);
    }
}

// ---- INTERNET and LAN ----

MCGamesScreen::MCGamesScreen(MCGamesWhere where) : _Where(where), _List(where)
{
    SetDocument(where == MCGamesWhere::Internet ? InternetDocument : LanDocument);
    Back = [] { MCScreenFlow::Back(); };
}

MCGamesScreen::~MCGamesScreen() = default;

std::string MCGamesScreen::Place() const
{
    return _Where == MCGamesWhere::Internet ? InternetPlace : LanPlace;
}

void MCGamesScreen::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, NameOf(_Where));
    MCRmlModel& model = *_Model;
    model.List("games", _Rows, std::pair{"name", &MCRow::Name}, std::pair{"players", &MCRow::Players},
               std::pair{"where", &MCRow::Where}, std::pair{"now", &MCRow::Now}, std::pair{"off", &MCRow::Off},
               std::pair{"needs", &MCRow::Needs});
    model.List("pick_lines", _PickLines, std::pair{"text", &MCLine::Text});
    model.List("pick_players", _PickPlayers, std::pair{"text", &MCPlayer::Text});
    model.Value("counts", _Counts);
    model.Value("empty", _Empty);
    model.Value("retry", _Retry);
    model.Value("update", _Update);
    model.Value("picked", _PickedRow);
    model.Value("pick_name", _PickName);
    model.Value("pick_count", _PickCount);
    model.Value("player_name", _PlayerName);
    model.Value("code", _Code);
    model.Value("can_join", _CanJoin);
    model.Value("can_host", _CanHost);
    model.Event("pick",
                [this](const Rml::VariantList& arguments)
                {
                    if (!arguments.empty())
                    {
                        PickRow(arguments.front().Get<int>());
                    }
                });
    model.Event("retry", [this](const Rml::VariantList&) { PressRetry(); });
    model.Event("update", [this](const Rml::VariantList&) { PressUpdate(); });
}

void MCGamesScreen::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("back",
            [this]
            {
                if (Back)
                {
                    Back();
                }
            });
    OnClick("join",
            [this]
            {
                if (!IsOff(Document(), "join"))
                {
                    PressJoin();
                }
            });
    OnClick("host",
            [this]
            {
                if (!IsOff(Document(), "host"))
                {
                    PressHost();
                }
            });
}

MCOptionalRef<Rml::Element> MCGamesScreen::FirstFocus(Rml::ElementDocument& document)
{
    // The picked game, or the list's first; with none, HOST GAME. The list's rows only (not the picked game's players),
    // its template left out.
    Rml::ElementList rows;

    if (Rml::Element* list = document.GetElementById(ListId); list != nullptr)
    {
        list->GetElementsByClassName(rows, RowClass);
    }

    std::erase_if(rows, [](Rml::Element* row) { return !row->IsVisible(); });
    const size_t picked = _PickedRow >= 0 ? static_cast<size_t>(_PickedRow) : 0;

    if (_List.State() == MCGamesState::Listed && picked < rows.size() && !_Rows.empty())
    {
        return MCOptionalRef<Rml::Element>(*rows[picked]);
    }

    // Offline or not answering: RETRY, which ENTER presses (the hint line's ENTER Retry), once it shows.
    if (Rml::Element* retry = document.GetElementById(RetryId); _Retry && retry != nullptr && retry->IsVisible())
    {
        return MCOptionalRef<Rml::Element>(*retry);
    }

    Rml::Element* host = document.GetElementById("host");
    return host != nullptr ? MCOptionalRef<Rml::Element>(*host) : std::nullopt;
}

void MCGamesScreen::OnShow()
{
    SDL_Log("%s: shown", Place().c_str());
    MCRmlDialogs::SetPlace(Place());
    _PlayerName = RememberedName();
    _Code.clear();
    _PickedKey.reset();
    _Entered = false;
    _Away = false;
    _FocusFirstRow = false;
    // A newer game picked to join after the update is no longer (back from UPDATE, or a new visit).
    MCLobbyUpdate::JoinAfterUpdate(std::nullopt);

    // This network's games are found over TCP/IP (the original asked "IPX or TCP?"; the port's IPX is the same search).
    if (_Where == MCGamesWhere::Lan)
    {
        if (const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
            game != nullptr && game->SessionManager != nullptr && game->SessionManager->CurrentSession() == nullptr)
        {
            game->SessionManager->ConnectTcp("");
        }
    }

    _List.Open();
    ShowList();

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCGamesScreen::OnHide()
{
    SDL_Log("%s: hidden", Place().c_str());
    // Its socket's close takes it off the service's count; a game joined is counted as it plays.
    _List.Close();

    // A join called off, its change with it (shown again over the screen that came up), unless it has joined.
    if (_Join.Pending())
    {
        CallOff();
    }

    if (_HostWindow)
    {
        _HostWindow->Hide();
    }

    MCRmlDialogs::Close();
}

void MCGamesScreen::OnDestroy()
{
    _List.Close();

    if (const std::shared_ptr<MCRmlScreen> window = _HostWindow.Lock(); window != nullptr)
    {
        window->Close();
    }

    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCGamesScreen::OnFrame()
{
    // A player away isn't counted online: the list's socket goes until the next input.
    if (_Where == MCGamesWhere::Internet)
    {
        if (MCIdleCheck::Away() && _List.IsOpen() && !_Join.Pending())
        {
            SDL_Log("INTERNET: the player is away: the list closes until the next input");
            _List.Close();
            _Away = true;
            ShowList();
        }
        else if (_Away && !MCIdleCheck::Away())
        {
            _List.Open();
            _Away = false;
            ShowList();
        }
    }

    if (_List.Update())
    {
        ShowList();
    }

    if (_Join.Pending())
    {
        Ended(_Join.Tend());
    }

    if (_FocusFirstRow)
    {
        FocusFirstRow();
    }

    ShowButtons();
}

void MCGamesScreen::FocusFirstRow()
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value() || (_Rows.empty() && !_Retry))
    {
        _FocusFirstRow = false;
        return;
    }

    // Laid out by the context's next update: until then there's no row to focus.
    const MCOptionalRef<Rml::Element> first = FirstFocus(document->get());
    Rml::Element* host = document->get().GetElementById("host");

    if (!first.has_value() || &first->get() == host)
    {
        return;
    }

    _FocusFirstRow = false;
    Rml::Context* context = document->get().GetContext();
    Rml::Element* focused = context != nullptr ? context->GetFocusElement() : nullptr;

    // Only from where the screen put it (HOST GAME, with no games): a box the player is typing in keeps it. The ring
    // as it was: a keys player sees it move to the game, a mouse player sees none.
    if (focused == nullptr || focused == host || focused == &document->get())
    {
        first->get().Focus(focused != nullptr && focused->IsPseudoClassSet(FocusVisible));
    }
}

void MCGamesScreen::ShowList()
{
    const MCGamesState state = _Away ? MCGamesState::Looking : _List.State();
    std::vector<MCRow> rows;

    if (state == MCGamesState::Listed)
    {
        for (const MCListedGame& game : _List.Games())
        {
            rows.push_back(MCRow{.Name = game.Name,
                                 .Players = game.PlayersText(),
                                 .Where = game.Region,
                                 .Now = game.NowText(),
                                 .Off = game.InBattle || game.Full(),
                                 .Needs = game.NeedsNewer && !game.InBattle});
        }
    }

    std::string counts;

    if (_Where == MCGamesWhere::Internet)
    {
        const std::optional<std::pair<int32_t, int32_t>> listed = _List.Counts();
        counts = listed.has_value()               ? std::format(CountsFormat, listed->first, listed->second)
                 : state == MCGamesState::Offline ? std::string(OfflineCounts)
                                                  : std::string();
    }
    else if (state == MCGamesState::Listed && !rows.empty())
    {
        counts = rows.size() == 1 ? std::string(LanOneCount) : std::format(LanCountFormat, rows.size());
    }

    std::string empty;

    switch (state)
    {
        case MCGamesState::Looking:
            empty = LookingText;
            break;
        case MCGamesState::Offline:
            empty = OfflineText;
            break;
        case MCGamesState::NotAnswering:
            empty = NotAnsweringText;
            break;
        case MCGamesState::Update:
            empty = UpdateText;
            break;
        case MCGamesState::Blocked:
            empty = BlockedText;
            break;
        case MCGamesState::Unavailable:
            empty = UnavailableText;
            break;
        case MCGamesState::Listed:
        {
            if (rows.empty())
            {
                // "No open games." alone: no busiest hours until there's enough to go by (the user, 23 Nov).
                empty = _Where == MCGamesWhere::Internet ? NoGamesText : NoLanGamesText;
            }

            break;
        }
    }

    // The pick stays on its game; gone from the list, nothing is picked.
    if (_PickedKey.has_value() && !_List.Find(*_PickedKey).has_value())
    {
        _PickedKey.reset();
    }

    int picked = -1;

    if (_PickedKey.has_value())
    {
        const std::span<const MCListedGame> games = _List.Games();
        const auto found = std::ranges::find(games, *_PickedKey, &MCListedGame::Key);
        picked = found != games.end() ? static_cast<int>(found - games.begin()) : -1;
    }

    const bool retry = state == MCGamesState::Offline || state == MCGamesState::NotAnswering;
    const bool update = state == MCGamesState::Update;
    // The first rows to come: the focus goes to the first game, as it would have shown with them (FirstFocus), once
    // they're laid out.
    _FocusFirstRow = _FocusFirstRow || (_Rows.empty() && !rows.empty()) || (retry && !_Retry);
    bool changed = false;
    // Each value at once (no short-circuit): every one kept.
    changed |= Keep(_Rows, std::move(rows));
    changed |= Keep(_Counts, std::move(counts));
    changed |= Keep(_Empty, std::move(empty));
    changed |= Keep(_Retry, retry);
    changed |= Keep(_Update, update);
    changed |= Keep(_PickedRow, picked);

    if (changed && _Model != nullptr)
    {
        _Model->Changed("games");
        _Model->Changed("counts");
        _Model->Changed("empty");
        _Model->Changed("retry");
        _Model->Changed("update");
        _Model->Changed("picked");
    }

    // The keys the hint line names, as the list stands.
    std::vector<MCRmlKeyHint> keys;

    if (!_Rows.empty())
    {
        keys.push_back({"↑ ↓", "Pick a game"});
        keys.push_back({"ENTER", "Join game"});
    }
    else if (retry)
    {
        keys.push_back({"ENTER", "Retry"});
    }

    keys.push_back({"TAB", "Next box"});
    keys.push_back({"ESC", "Back"});

    const auto same = [](const MCRmlKeyHint& a, const MCRmlKeyHint& b) { return a.Key == b.Key && a.Does == b.Does; };

    if (!std::ranges::equal(keys, Keys, same))
    {
        Keys = std::move(keys);
        ShowKeys();
    }

    ShowPick();
    ShowButtons();
}

void MCGamesScreen::ShowPick()
{
    const std::optional<MCListedGame> game = Picked();
    std::string name;
    std::string count;
    std::vector<MCLine> lines;
    std::vector<MCPlayer> players;

    if (game.has_value())
    {
        name = game->Name;
        count = game->PlayersText();

        if (_Where == MCGamesWhere::Lan)
        {
            lines.push_back({std::string(LanLine)});
        }
        else
        {
            lines.push_back(
                {game->Region.empty() ? std::string(PublicLine) : std::format(PublicHostedFormat, game->Region)});
        }

        if (game->InBattle)
        {
            lines.push_back({game->Mission.empty() ? game->NowText()
                                                   : std::format(OnMissionFormat, game->NowText(), game->Mission)});
        }
        else if (game->NeedsNewer)
        {
            lines.push_back({game->NowText()});
        }

        for (const std::string& player : game->Players)
        {
            players.push_back({player});
        }
    }

    bool changed = Keep(_PickName, std::move(name));
    changed |= Keep(_PickCount, std::move(count));
    changed |= Keep(_PickLines, std::move(lines));
    changed |= Keep(_PickPlayers, std::move(players));

    if (changed && _Model != nullptr)
    {
        _Model->Changed("pick_name");
        _Model->Changed("pick_count");
        _Model->Changed("pick_lines");
        _Model->Changed("pick_players");
    }
}

std::optional<std::string> MCGamesScreen::TypedCode() const
{
    if (_Where != MCGamesWhere::Internet)
    {
        return std::nullopt;
    }

    std::string code = Upper(_Code);
    std::erase(code, ' ');
    return code.size() == CodeLength ? std::optional(code) : std::nullopt;
}

std::optional<MCListedGame> MCGamesScreen::Picked() const
{
    return _PickedKey.has_value() ? _List.Find(*_PickedKey) : std::nullopt;
}

void MCGamesScreen::ShowButtons()
{
    const MCGamesState state = _List.State();
    const std::optional<MCListedGame> picked = Picked();
    const bool joinable = picked.has_value() && (picked->Joinable() || (picked->NeedsNewer && !picked->InBattle));
    const bool canJoin = !_Join.Pending() && (TypedCode().has_value() || joinable);
    const bool canHost = !_Join.Pending() && (_Where == MCGamesWhere::Lan || state == MCGamesState::Listed ||
                                              state == MCGamesState::Looking);
    const bool changedJoin = Keep(_CanJoin, canJoin);
    const bool changedHost = Keep(_CanHost, canHost);

    if (_Model != nullptr && (changedJoin || changedHost))
    {
        _Model->Changed("can_join");
        _Model->Changed("can_host");
    }
}

MCGamesScreen::MCSeen MCGamesScreen::Seen() const
{
    MCSeen shown{.Counts = _Counts,
                 .Empty = _Rows.empty() ? _Empty : std::string(),
                 .Retry = _Retry,
                 .Update = _Update,
                 .Picked = _PickedRow,
                 .PickName = _PickName,
                 .CanJoin = _CanJoin,
                 .CanHost = _CanHost};

    for (const MCRow& row : _Rows)
    {
        shown.Rows.push_back(_Where == MCGamesWhere::Internet
                                 ? std::format("{} | {} | {} | {}", row.Name, row.Players, row.Where, row.Now)
                                 : std::format("{} | {} | {}", row.Name, row.Players, row.Now));
    }

    for (const MCLine& line : _PickLines)
    {
        shown.PickLines.push_back(line.Text);
    }

    for (const MCPlayer& player : _PickPlayers)
    {
        shown.PickPlayers.push_back(player.Text);
    }

    return shown;
}

void MCGamesScreen::SetPlayerName(std::string name)
{
    _PlayerName = std::move(name);

    if (_Model != nullptr)
    {
        _Model->Changed("player_name");
    }
}

void MCGamesScreen::SetCode(std::string code)
{
    _Code = std::move(code);

    if (_Model != nullptr)
    {
        _Model->Changed("code");
    }

    ShowButtons();
}

void MCGamesScreen::PickRow(int32_t row)
{
    const std::span<const MCListedGame> games = _List.Games();

    if (row < 0 || static_cast<size_t>(row) >= games.size() || _Rows.empty())
    {
        return;
    }

    PickKey(games[static_cast<size_t>(row)].Key);
}

void MCGamesScreen::PickKey(std::string_view key)
{
    if (!_List.Find(key).has_value())
    {
        return;
    }

    _PickedKey = std::string(key);
    ShowList();
}

void MCGamesScreen::PressRetry()
{
    _List.Retry();
    ShowList();
}

void MCGamesScreen::PressUpdate()
{
    OpenUpdate(std::nullopt);
}

void MCGamesScreen::PressJoin()
{
    if (_Join.Pending())
    {
        return;
    }

    SaveUserName(_PlayerName);
    MCGameJoin::MCAsk ask{.Where = _Where, .PlayerName = _PlayerName};

    if (const std::optional<std::string> code = TypedCode())
    {
        const std::optional<MCListedGame> listed = _List.Find(*code);
        ask.Key = *code;
        // A game on the service's list is public: its links take relays only, whatever the service says after (its
        // host can't make it private for those who found it there).
        ask.Typed = !listed.has_value();
        ask.Shown = listed.has_value() ? listed->Name : std::format(MCGameJoin::CodeGameFormat, *code);
    }
    else if (const std::optional<MCListedGame> picked = Picked(); picked.has_value())
    {
        if (picked->NeedsNewer && !picked->InBattle)
        {
            SayNeedsNewer(*picked);
            return;
        }

        if (!picked->Joinable())
        {
            return;
        }

        ask.Key = picked->Key;
        ask.Shown = picked->Name;
    }
    else
    {
        return;
    }

    Begin(std::move(ask));
}

void MCGamesScreen::PressHost()
{
    if (_Join.Pending())
    {
        return;
    }

    SaveUserName(_PlayerName);

    if (!_HostWindow)
    {
        const std::weak_ptr<MCRmlScreen> self = weak_from_this();
        _HostWindow = Layer().CreateChild<MCHostWindow>(
            [self](MCHostWindow& made)
            {
                made.Hosting = [self](const MCHostWindow::MCHost& host)
                {
                    if (const std::shared_ptr<MCRmlScreen> screen = self.lock(); screen != nullptr)
                    {
                        static_cast<MCGamesScreen&>(*screen).Host(host);
                    }
                };
            },
            _Where);
    }

    _HostWindow->OpenWith(std::format(GameNameFormat, _PlayerName.empty() ? NoPlayerName : _PlayerName));
}

void MCGamesScreen::Host(const MCHostWindow::MCHost& host)
{
    if (_HostWindow)
    {
        _HostWindow->Hide();
    }

    _LastHost = host;
    Begin(MCGameJoin::MCAsk{.Where = _Where,
                            .Hosting = true,
                            .Shown = host.GameName,
                            .PlayerName = _PlayerName,
                            .GameName = host.GameName,
                            .MaxPlayers = host.MaxPlayers,
                            .Public = host.Public});
}

void MCGamesScreen::Begin(MCGameJoin::MCAsk ask)
{
    _Entered = false;

    if (!_Join.Begin(std::move(ask)))
    {
        SayRefusal();
        return;
    }

    // This network's game: joined or made at once, as the 1998 LAN screen's were.
    if (_Where == MCGamesWhere::Lan)
    {
        Ended(_Join.Tend());
        return;
    }

    // The service's: a job with the list live under its wait ("Joining Steel Rain..." with CANCEL); joined, the list
    // fades out and the room opens dark.
    const MCGameJoin::MCAsk& asked = *_Join.Asked();
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = Place(),
                                          .Text = asked.Hosting ? std::string(MCGameJoin::HostingText)
                                                                : std::format(MCGameJoin::JoiningFormat, asked.Shown),
                                          .Main = {},
                                          .Other = std::string(CancelWord),
                                          .OnOther = [self]
                                          {
                                              if (const std::shared_ptr<MCRmlScreen> screen = self.lock();
                                                  screen != nullptr)
                                              {
                                                  static_cast<MCGamesScreen&>(*screen).CallOff();
                                              }
                                          }});
    const auto entered = [self]
    {
        const std::shared_ptr<MCRmlScreen> screen = self.lock();
        return screen == nullptr || static_cast<MCGamesScreen&>(*screen)._Entered ||
               !static_cast<MCGamesScreen&>(*screen)._Join.Pending();
    };

    MCScreenChange::Start({.Name = std::string(JoiningChange),
                           .Job = {{.Part = {}, .Until = entered}},
                           .FadeOut = MCScreenChange::MCFadeOut::AfterJob,
                           .Dark = {{.Part = "change.screen",
                                     .Run =
                                         [self]
                                     {
                                         const std::shared_ptr<MCRmlScreen> screen = self.lock();

                                         if (screen == nullptr || !static_cast<MCGamesScreen&>(*screen)._Entered)
                                         {
                                             return;
                                         }

                                         static_cast<MCGamesScreen&>(*screen)._Entered = false;
                                         MCRmlDialogs::Close();
                                         MCMenuScreens::ShowPreparedRoom();
                                         MCScreenFlow::Noted();
                                     }}},
                           .CalledOff = [self]
                           {
                               if (const std::shared_ptr<MCRmlScreen> screen = self.lock(); screen != nullptr)
                               {
                                   static_cast<MCGamesScreen&>(*screen).CallOff();
                               }
                           }});
}

void MCGamesScreen::Ended(MCGameJoin::MCEnd end)
{
    switch (end)
    {
        case MCGameJoin::MCEnd::None:
        case MCGameJoin::MCEnd::CalledOff:
            return;
        case MCGameJoin::MCEnd::Joined:
        {
            // The room set up at once (what the host says next lands in it); shown once the list is dark, by the join's
            // change, or with none (no display, a LAN game) through a change of its own.
            if (MCScreenChange::Name() == JoiningChange && MCScreenChange::Running())
            {
                MCMenuScreens::PrepareRoom();
                _Entered = true;
                return;
            }

            MCRmlDialogs::Close();
            MCMenuScreens::OpenRoom("room");
            return;
        }
        case MCGameJoin::MCEnd::Refused:
        {
            // The change the join ran in called off, the list staying up, then why said over it.
            if (MCScreenChange::Name() == JoiningChange && MCScreenChange::Running())
            {
                MCScreenChange::Cancel();
            }

            SayRefusal();
            return;
        }
    }
}

void MCGamesScreen::SayRefusal()
{
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    const auto me = [self]() -> MCGamesScreen*
    {
        const std::shared_ptr<MCRmlScreen> screen = self.lock();
        return screen != nullptr ? &static_cast<MCGamesScreen&>(*screen) : nullptr;
    };

    MCRmlDialog::MCSay say{.Eyebrow = Place(), .Text = _Join.Refusal(), .Main = std::string(OkWord)};

    if (_Join.CanRetry() && _Join.Asked().has_value())
    {
        say.Main = RetryWord;
        say.Other = CancelWord;
        say.OnMain = [me, asked = *_Join.Asked()]
        {
            if (MCGamesScreen* screen = me(); screen != nullptr)
            {
                screen->Begin(asked);
            }
        };
    }
    else if (_Join.NeedsUpdate())
    {
        say.Main = UpdateWord;
        say.Other = CancelWord;
        say.OnMain = [me]
        {
            if (MCGamesScreen* screen = me(); screen != nullptr)
            {
                screen->OpenUpdate(std::nullopt);
            }
        };
    }

    MCRmlDialogs::Show(std::move(say));
}

void MCGamesScreen::SayNeedsNewer(const MCListedGame& game)
{
    const std::string ours = MCInternetGame::SetupNow().Version;
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = game.Name,
                                          .Heading = std::format(NeedsFormat, game.Release()),
                                          .Text = std::format(YouHaveFormat, ours.substr(0, ours.find(' '))),
                                          .Main = std::string(UpdateWord),
                                          .Other = std::string(CancelWord),
                                          .OnMain = [self, game]
                                          {
                                              if (const std::shared_ptr<MCRmlScreen> screen = self.lock();
                                                  screen != nullptr)
                                              {
                                                  static_cast<MCGamesScreen&>(*screen).OpenUpdate(game);
                                              }
                                          }});
}

void MCGamesScreen::OpenUpdate(std::optional<MCListedGame> joinAfter)
{
    // A newer game: the update first, its new version joining the game (network/MCLobbyUpdate.h). Set after the screen
    // hides (its showing again drops it).
    std::optional<MCLobbyUpdate::MCJoinAfter> after;

    if (joinAfter.has_value())
    {
        after = MCLobbyUpdate::MCJoinAfter{
            .Code = joinAfter->Key, .Name = joinAfter->Name, .PlayerName = _PlayerName, .RelayOnly = true};
    }

    if (!MCScreenFlow::Open(MCMenuScreens::Update()))
    {
        SDL_Log("%s: UPDATE can't open now (a change of screen runs)", Place().c_str());
        return;
    }

    MCLobbyUpdate::JoinAfterUpdate(std::move(after));
}

void MCGamesScreen::CallOff()
{
    // Joined, the room set up and shown once the list is dark: too late to call off (the old screen's CANCEL ended the
    // game; here the room just shows).
    if (_Entered)
    {
        return;
    }

    if (_Join.Pending())
    {
        _Join.CallOff();
        MCRmlDialogs::Close();
    }

    if (MCScreenChange::Name() == JoiningChange && MCScreenChange::Running() && !MCScreenChange::Dark())
    {
        MCScreenChange::Cancel();
    }
}

// ---- the flow's kinds ----

MCScreenFlow::MCScreenKind MCGamesKind(MCGamesWhere where)
{
    // Port-only (stage 7): BACK goes to MULTIPLAYER (the main menu's panel), which lists LAN alone on a build with no
    // internet games.
    return MCRmlFlow::Kind(
        NameOf(where), [where](MCRmlLayer& layer)
        { return MCGuiHandle<MCRmlScreen>(layer.CreateChild<MCGamesScreen>([](MCGamesScreen&) {}, where)); },
        MCRmlFlow::MCKeep::Kept);
}

MCGuiHandle<MCGamesScreen> MCGamesScreenOf(MCGamesWhere where)
{
    const MCGuiHandle<MCRmlScreen> made = MCRmlFlow::Made(NameOf(where));
    const std::shared_ptr<MCRmlScreen> screen = made.Lock();
    return screen != nullptr ? MCGuiHandle<MCGamesScreen>(std::static_pointer_cast<MCGamesScreen>(screen))
                             : MCGuiHandle<MCGamesScreen>{};
}

namespace
{
    /// <summary>The logistics states the 1998 INTERNET and LAN screens had (the main menu's notices wait for its own).</summary>
    constexpr int32_t InternetState = 10;
    constexpr int32_t LanState = 0xb;
}

void ShowGamesNow(MCGamesWhere where)
{
    const MCOptionalRef<const MCScreenFlow::MCScreenKind> kind = MCScreenFlow::Kind(IdOf(where));

    if (!kind.has_value())
    {
        return;
    }

    if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
    {
        logistics->HideScreen();

        // The remade screens stand over the main menu, hidden: logistics' screen, so that an old one shown from them
        // (the room) is a change of screen, which hides them (MCLogistics::SetCurrentScreen).
        if (logistics->MainScreen && MCMenuScreens::Top() != MCMenuScreens::Main())
        {
            logistics->SetCurrentScreen(logistics->MainScreen);
        }

        logistics->LogisticsState = where == MCGamesWhere::Internet ? InternetState : LanState;
    }

    // Copied: showing it may register the kinds again (the menus made again).
    const std::function<void()> show = kind->get().Show;

    if (show != nullptr)
    {
        show();
    }
}

void ShowInternetScreen()
{
    ShowGamesNow(MCGamesWhere::Internet);
}

void ShowLanScreen()
{
    ShowGamesNow(MCGamesWhere::Lan);
}

void DoTheTcpThang()
{
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        MultiPlayer()->SessionManager->ConnectTcp(const_cast<char*>(""));
    }

    // Port-only (SPEC-menus.md's stage 3): the remade LAN screen, shown at once.
    ShowLanScreen();
}

void ReleaseGamesScreens()
{
    for (const MCGamesWhere where : {MCGamesWhere::Internet, MCGamesWhere::Lan})
    {
        if (const MCGuiHandle<MCGamesScreen> screen = MCGamesScreenOf(where); screen)
        {
            screen->List().Close();
        }
    }
}
