#include "stdafx.h"
#include "logistics/MCGamesScreen.h"
#include "gui/MCScreenChange.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCChatView.h"
#include "logistics/MCIdleCheck.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRegistrySettings.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMcoRooms.h"
#include "network/MCMcoSeats.h"
#include "network/MCMultiPlayer.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>
#include <cctype>

namespace
{
    constexpr const char* InternetDocument = "internet.rml";
    constexpr const char* LanDocument = "lan.rml";
    constexpr const char* HostDocument = "host_game.rml";
    constexpr const char* PasswordDocument = "join_password.rml";
    /// <summary>Port-only (Redux HD): a private game's code's window (SPEC-in-game-lobby.md), the password's shape.</summary>
    constexpr const char* CodeDocument = "join_code.rml";
    /// <summary>The locked game's windows' box: the password's, or the code's.</summary>
    constexpr const char* PasswordBoxId = "password";
    constexpr const char* CodeBoxId = "code";
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
    /// <summary>Port-only (Redux HD; SPEC-in-game-lobby.md): a private game's line, as a public one's.</summary>
    constexpr std::string_view PrivateLine = "Private";
    constexpr std::string_view PrivateHostedFormat = "Private, hosted in {}";
    /// <summary>MCO's rooms (SPEC-mco-rooms.md): a locked one's line, its players' column, and whom the room waits for.</summary>
    constexpr std::string_view LockedLine = "Needs a password";
    constexpr std::string_view ReadyText = "Ready";
    constexpr std::string_view NotReadyText = "Not ready";
    constexpr std::string_view WaitingFormat = "Waiting for {} to start.";
    /// <summary>MCO's room's chat box (INTERNET's only).</summary>
    constexpr const char* RoomSayId = "room-say";
    constexpr std::string_view SomeHost = "the host";
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
    /// <summary>PLAYER NAME's box.</summary>
    constexpr const char* NameId = "name";
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

    /// <summary>A code as typed (the CODE box's, a private game's window's): upper-cased, its spaces gone.</summary>
    std::string CodeAsTyped(std::string_view typed)
    {
        std::string code = Upper(typed);
        std::erase(code, ' ');
        return code;
    }

    /// <summary>
    /// Whether INTERNET shows MCO's room <paramref name="seen"/> is in: never the internet game's own (hosted on both),
    /// which is the room screen's, not even as it's let go after LEAVE.
    /// </summary>
    bool ShowsMcoRoom(const MCMcoSeen& seen)
    {
        return seen.Room.has_value() && !seen.Room->Both;
    }

    /// <summary>
    /// The player's name as a games screen opens: LAN's the one remembered, else the computer's account name (as the
    /// original), else "Player"; INTERNET's only the one remembered, never the account name, so it never goes online.
    /// </summary>
    std::string RememberedName(MCGamesWhere where)
    {
        if (where == MCGamesWhere::Internet)
        {
            return SavedPlayerName().value_or(std::string());
        }

        return MyGetUserName().value_or(std::string(NoPlayerName));
    }

    /// <summary>Whether <paramref name="name"/> has more than spaces.</summary>
    bool Named(std::string_view name)
    {
        return std::ranges::any_of(name, [](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; });
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

    /// <summary>The chat of the MCO match joined given its room's lines (<paramref name="seen"/>'s), as INTERNET showed them.</summary>
    void CarryRoomChat(const MCMcoSeen& seen)
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (!seen.Room.has_value() || logistics == nullptr || logistics->Chat == nullptr)
        {
            return;
        }

        for (const std::string& line : MCMcoSeats::RoomLinesOf(seen.Room->Chat))
        {
            if (const std::string bytes = MCChatView::AsChatBytes(line); !bytes.empty())
            {
                logistics->Chat->AddLine(bytes);
            }
        }
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

// ---- a locked game's password or code ----

MCPasswordWindow::MCPasswordWindow(MCAsks asks) : _Asks(asks)
{
    SetDocument(asks == MCAsks::Code ? CodeDocument : PasswordDocument);
    Modal = true;
    Keys = {{"ENTER", "Join game"}, {"ESC", "Cancel"}};
    Back = [this]
    {
        SetTyped({});
        Hide();
    };
}

void MCPasswordWindow::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("cancel", [this] { Back(); });
    OnClick("join", [this] { PressJoin(); });
    // A code's JOIN GAME follows what's typed, a letter at a time.
    Listen(_Asks == MCAsks::Code ? CodeBoxId : PasswordBoxId, Rml::EventId::Change,
           [this](Rml::Event&) { ShowJoin(); });
}

void MCPasswordWindow::OpenFor(std::string roomName)
{
    _RoomName = std::move(roomName);

    if (!Document().has_value())
    {
        if (const auto opened = Open(); !opened)
        {
            MCRmlFlow::Broken(std::format("the {}'s window can't open: {}",
                                          _Asks == MCAsks::Code ? CodeBoxId : PasswordBoxId, opened.error()));
            return;
        }
    }
    else
    {
        Show();
    }

    if (Rml::Element* room = Document()->get().GetElementById("room"); room != nullptr)
    {
        room->SetInnerRML(MCRmlDialog::Escape(_RoomName));
    }

    SetTyped({});
}

std::string MCPasswordWindow::Typed() const
{
    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        if (Rml::Element* box = document->get().GetElementById(_Asks == MCAsks::Code ? CodeBoxId : PasswordBoxId);
            box != nullptr)
        {
            return ValueOf(*box);
        }
    }

    return {};
}

bool MCPasswordWindow::CanJoin() const
{
    return _Asks == MCAsks::Password || CodeAsTyped(Typed()).size() == CodeLength;
}

void MCPasswordWindow::ShowJoin()
{
    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        if (Rml::Element* join = document->get().GetElementById("join"); join != nullptr)
        {
            join->SetClass(OffClass, !CanJoin());
        }
    }
}

void MCPasswordWindow::SetTyped(std::string_view typed)
{
    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        if (Rml::Element* box = document->get().GetElementById(_Asks == MCAsks::Code ? CodeBoxId : PasswordBoxId);
            box != nullptr)
        {
            box->SetAttribute(ValueAttribute, std::string(typed));
        }
    }

    ShowJoin();
}

void MCPasswordWindow::PressJoin()
{
    if (!CanJoin())
    {
        return;
    }

    std::string typed = Typed();
    SetTyped({});
    Hide();

    if (Joining)
    {
        Joining(std::move(typed));
    }
}

// ---- INTERNET and LAN ----

MCGamesScreen::MCGamesScreen(MCGamesWhere where) : _Where(where), _List(where)
{
    SetDocument(where == MCGamesWhere::Internet ? InternetDocument : LanDocument);
    // In MCO's room, ESC leaves it (the list again), never the screen with the player still in the room.
    Back = [this]
    {
        if (_InRoom)
        {
            PressLeave();
            return;
        }

        MCScreenFlow::Back();
    };
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
    model.List("room_players", _RoomPlayers, std::pair{"name", &MCRoomRow::Name}, std::pair{"now", &MCRoomRow::Now},
               std::pair{"ready", &MCRoomRow::Ready}, std::pair{"host", &MCRoomRow::Host});
    model.List("room_chat", _RoomChat, std::pair{"name", &MCRoomChatLine::Name},
               std::pair{"text", &MCRoomChatLine::Text}, std::pair{"event", &MCRoomChatLine::Event});
    model.Value("in_room", _InRoom);
    model.Value("room_name", _RoomName);
    model.Value("room_line", _RoomLine);
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

void MCGamesScreen::OnLoaded(Rml::ElementDocument& document)
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

    // LEAVE and the room's chat: INTERNET's only (MCO's room); LAN has neither.
    if (document.GetElementById("leave") != nullptr)
    {
        OnClick("leave", [this] { PressLeave(); });
    }

    if (document.GetElementById(RoomSayId) != nullptr)
    {
        OnText(RoomSayId,
               [this](const std::string& typed, bool enter)
               {
                   if (!enter)
                   {
                       return;
                   }

                   if (MCMcoRooms::Available())
                   {
                       MCMcoRooms::Now().Say(typed);
                   }

                   const MCOptionalRef<Rml::ElementDocument> shown = Document();

                   if (Rml::Element* box = shown.has_value() ? shown->get().GetElementById(RoomSayId) : nullptr)
                   {
                       box->SetAttribute(ValueAttribute, std::string());
                   }
               });
    }
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

    // In MCO's room: its chat's box, what a player waiting for the host does (as the room screen's; LEAVE is ESC).
    // Laid out by the context's next update: until it shows, none (FocusFirstRow tries again).
    if (_InRoom)
    {
        Rml::Element* say = document.GetElementById(RoomSayId);
        return say != nullptr && say->IsVisible(true) ? MCOptionalRef<Rml::Element>(*say) : std::nullopt;
    }

    // INTERNET with no name yet: its box, which HOST GAME and JOIN GAME wait on.
    if (_Where == MCGamesWhere::Internet && !Named(_PlayerName))
    {
        Rml::Element* name = document.GetElementById(NameId);
        return name != nullptr ? MCOptionalRef<Rml::Element>(*name) : std::nullopt;
    }

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
    _PlayerName = RememberedName(_Where);
    _List.SetGuestName(_PlayerName);
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
    // MCO read afresh: back from its match, the player is in its room again.
    _McoRevision = 0;
    TendMco();
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

    TendMco();

    if (_FocusFirstRow)
    {
        FocusFirstRow();
    }

    ShowButtons();
}

void MCGamesScreen::FocusFirstRow()
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value() || (_Rows.empty() && !_Retry && !_InRoom))
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

    // Only from where the screen put it (HOST GAME, with no games) or from what no longer shows (JOIN GAME, the list's
    // rows, once in MCO's room): a box the player is typing in keeps it. The ring as it was: a keys player sees it move
    // to the game, a mouse player sees none.
    if (focused == nullptr || focused == host || focused == &document->get() || !focused->IsVisible(true))
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

    if (_InRoom)
    {
        keys.push_back({"ENTER", "Say"});
        keys.push_back({"ESC", "Leave"});
    }
    else
    {
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
    }

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
            if (game->Private)
            {
                lines.push_back(
                    {game->Region.empty() ? std::string(PrivateLine) : std::format(PrivateHostedFormat, game->Region)});
            }
            else
            {
                const std::string_view open = game->Locked ? LockedLine : PublicLine;
                lines.push_back(
                    {game->Region.empty() ? std::string(open) : std::format(PublicHostedFormat, game->Region)});
            }
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

    std::string code = CodeAsTyped(_Code);
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
    // INTERNET's HOST GAME and JOIN GAME wait for a name (it starts with none, never the computer's account name).
    const bool named = _Where == MCGamesWhere::Lan || Named(_PlayerName);
    const bool canJoin = named && !_Join.Pending() && (TypedCode().has_value() || joinable);
    const bool canHost =
        named && !_Join.Pending() &&
        (_Where == MCGamesWhere::Lan || state == MCGamesState::Listed || state == MCGamesState::Looking);
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

    shown.InRoom = _InRoom;
    shown.RoomName = _RoomName;
    shown.RoomLine = _RoomLine;

    for (const MCRoomRow& player : _RoomPlayers)
    {
        shown.RoomPlayers.push_back(player.Host ? std::format("{} | HOST | {}", player.Name, player.Now)
                                                : std::format("{} | {}", player.Name, player.Now));
    }

    for (const MCRoomChatLine& line : _RoomChat)
    {
        shown.RoomChat.push_back(line.Event ? line.Text : std::format("{}: {}", line.Name, line.Text));
    }

    return shown;
}

void MCGamesScreen::SetPlayerName(std::string name)
{
    _PlayerName = std::move(name);
    _List.SetGuestName(_PlayerName);

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
        // A private game: its code asked first (the list doesn't carry it), a newer one's update after.
        if (picked->Private)
        {
            if (!picked->InBattle && !picked->Full())
            {
                AskCode(*picked);
            }

            return;
        }

        if (picked->NeedsNewer && !picked->InBattle)
        {
            SayNeedsNewer(*picked);
            return;
        }

        if (!picked->Joinable())
        {
            return;
        }

        // MCO's room: joined through MCO, its password asked first when it's locked.
        if (!picked->McoRoom.empty())
        {
            if (picked->Locked)
            {
                AskPassword(*picked);
                return;
            }

            JoinMco(*picked, {});
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
    // MCO's match: "Connecting to the match..." as MCO's own launcher's game connects.
    const std::string waiting = asked.Where == MCGamesWhere::Mco ? std::string(MCMcoRooms::ConnectingText)
                                : asked.Hosting                  ? std::string(MCGameJoin::HostingText)
                                                                 : std::format(MCGameJoin::JoiningFormat, asked.Shown);
    MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = Place(),
                                          .Text = waiting,
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
            // MCO's match: the game is in it (its room polled saying so, as MCO's launcher's), and what was said in MCO's
            // room carries on in the match's chat, which its room and briefing show, as a Redux HD room's does.
            if (_Join.Asked()->Where == MCGamesWhere::Mco && MCMcoRooms::Available())
            {
                MCMcoRooms::Now().GameInMatch();
                CarryRoomChat(MCMcoRooms::Now().Seen());
            }

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

            // MCO's match not joined: MCO's room says so (TendMco), the player left in it.
            if (_Join.Asked()->Where == MCGamesWhere::Mco && MCMcoRooms::Available())
            {
                MCRmlDialogs::Close();
                MCMcoRooms::Now().GameFailed();
                return;
            }

            SayRefusal();
            return;
        }
    }
}

void MCGamesScreen::SayRefusal()
{
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    // A pin on this screen (null once it is gone), held by the caller for the length of its call (gui/MCGuiRef.h).
    const auto me = [self]() -> std::shared_ptr<MCGamesScreen>
    { return std::static_pointer_cast<MCGamesScreen>(self.lock()); };

    MCRmlDialog::MCSay say{.Eyebrow = Place(), .Text = _Join.Refusal(), .Main = std::string(OkWord)};

    if (_Join.CanRetry() && _Join.Asked().has_value())
    {
        say.Main = RetryWord;
        say.Other = CancelWord;
        say.OnMain = [me, asked = *_Join.Asked()]
        {
            if (const std::shared_ptr<MCGamesScreen> screen = me(); screen != nullptr)
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
            if (const std::shared_ptr<MCGamesScreen> screen = me(); screen != nullptr)
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
        // A listed public game takes relays only; a private one (its code typed) as the service says.
        after = MCLobbyUpdate::MCJoinAfter{.Code = joinAfter->Key,
                                           .Name = joinAfter->Name,
                                           .PlayerName = _PlayerName,
                                           .RelayOnly = !joinAfter->Private};
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

        // MCO's match called off: out of its room too (the list again).
        if (_Join.Asked()->Where == MCGamesWhere::Mco)
        {
            PressLeave();
        }
    }

    if (MCScreenChange::Name() == JoiningChange && MCScreenChange::Running() && !MCScreenChange::Dark())
    {
        MCScreenChange::Cancel();
    }
}

// ---- MechCommander Online's rooms ----

void MCGamesScreen::AskFor(const MCListedGame& game, MCPasswordWindow::MCAsks asks,
                           MCGuiHandle<MCPasswordWindow>& window,
                           void (MCGamesScreen::*join)(const MCListedGame&, std::string))
{
    _Locked = game;

    if (!window)
    {
        const std::weak_ptr<MCRmlScreen> self = weak_from_this();
        window = Layer().CreateChild<MCPasswordWindow>(
            [self, join](MCPasswordWindow& made)
            {
                made.Joining = [self, join](std::string typed)
                {
                    const std::shared_ptr<MCRmlScreen> screen = self.lock();

                    if (screen == nullptr)
                    {
                        return;
                    }

                    auto& games = static_cast<MCGamesScreen&>(*screen);

                    if (const std::optional<MCListedGame> locked = std::exchange(games._Locked, std::nullopt))
                    {
                        (games.*join)(*locked, std::move(typed));
                    }
                };
            },
            asks);
    }

    window->OpenFor(game.Name);
}

void MCGamesScreen::AskPassword(const MCListedGame& game)
{
    AskFor(game, MCPasswordWindow::MCAsks::Password, _PasswordWindow, &MCGamesScreen::JoinMco);
}

void MCGamesScreen::AskCode(const MCListedGame& game)
{
    AskFor(game, MCPasswordWindow::MCAsks::Code, _CodeWindow, &MCGamesScreen::JoinPrivate);
}

void MCGamesScreen::JoinPrivate(const MCListedGame& game, std::string typed)
{
    if (_Join.Pending())
    {
        return;
    }

    const std::string code = CodeAsTyped(typed);

    // The window's JOIN GAME waits for a code's length: anything else is no code to try.
    if (code.size() != CodeLength)
    {
        SDL_Log("%s: the private game %s's code isn't a code's length (%zu characters); not joined", Place().c_str(),
                game.Name.c_str(), code.size());
        return;
    }

    // A newer game: the update first, its restart joining it by the code typed.
    if (game.NeedsNewer)
    {
        MCListedGame byCode = game;
        byCode.Key = code;
        SayNeedsNewer(byCode);
        return;
    }

    SDL_Log("%s: joining the private game %s by the code typed", Place().c_str(), game.Name.c_str());
    // The code typed, as the CODE box's: a private game's links go direct where they can (the service says if not).
    Begin(
        MCGameJoin::MCAsk{.Where = _Where, .Shown = game.Name, .Key = code, .Typed = true, .PlayerName = _PlayerName});
}

void MCGamesScreen::JoinMco(const MCListedGame& game, std::string password)
{
    if (!MCMcoRooms::Available())
    {
        return;
    }

    SDL_Log("INTERNET: joining MechCommander Online's room %s", game.Name.c_str());
    _McoAsked = true;
    _McoRefusalSaid.clear();
    MCMcoRooms::Now().Join(game.McoRoom, std::move(password), _PlayerName);
    ShowMcoWait(std::format(MCMcoRooms::JoiningFormat, game.Name));
}

void MCGamesScreen::PressLeave()
{
    if (!MCMcoRooms::Available())
    {
        return;
    }

    SDL_Log("INTERNET: leaving MechCommander Online's room");
    _McoAsked = false;
    MCMcoRooms::Now().Leave();
}

void MCGamesScreen::ShowMcoWait(std::string text)
{
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    _McoWaiting = true;
    MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = Place(),
                                          .Text = std::move(text),
                                          .Main = {},
                                          .Other = std::string(CancelWord),
                                          .OnOther = [self]
                                          {
                                              if (const std::shared_ptr<MCRmlScreen> screen = self.lock();
                                                  screen != nullptr)
                                              {
                                                  auto& games = static_cast<MCGamesScreen&>(*screen);
                                                  games._McoWaiting = false;
                                                  games.PressLeave();
                                              }
                                          }});
}

void MCGamesScreen::TendMco()
{
    if (_Where != MCGamesWhere::Internet || !MCMcoRooms::Available() || MCMcoRooms::Now().Revision() == _McoRevision)
    {
        return;
    }

    const MCMcoSeen seen = MCMcoRooms::Now().Seen();
    _McoRevision = seen.Revision;

    if (ShowsMcoRoom(seen))
    {
        _McoAsked = true;
    }

    ShowMcoRoom(seen);
    TendMcoMatch(seen);

    // A join's or a match's wait ends with the room, or with why not.
    if (_McoWaiting && !seen.Busy && (seen.Match == MCMcoMatchStage::None || !seen.Room.has_value()))
    {
        _McoWaiting = false;
        MCRmlDialogs::Close();
    }

    // Only what the player asked of MCO is said: its lobby's trouble stays out of INTERNET's way (its rooms just
    // aren't listed).
    if (!_McoAsked)
    {
        return;
    }

    if (seen.Refusal.empty())
    {
        _McoRefusalSaid.clear();
    }
    else if (!seen.Busy && seen.Refusal != _McoRefusalSaid)
    {
        _McoRefusalSaid = seen.Refusal;
        _McoAsked = seen.Room.has_value();
        MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = Place(), .Text = seen.Refusal, .Main = std::string(OkWord)});
    }

    if (seen.Left.empty())
    {
        _McoLeftSaid.clear();
    }
    else if (seen.Left != _McoLeftSaid)
    {
        _McoLeftSaid = seen.Left;
        _McoAsked = false;
        MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = Place(),
                                              .Text = seen.Left,
                                              .Main = std::string(OkWord),
                                              .OnMain = []
                                              {
                                                  if (MCMcoRooms::Available())
                                                  {
                                                      MCMcoRooms::Now().ClearLeft();
                                                  }
                                              }});
    }
}

void MCGamesScreen::ShowMcoRoom(const MCMcoSeen& seen)
{
    std::vector<MCRoomRow> players;
    std::vector<MCRoomChatLine> chat;
    std::string name;
    std::string line;
    const bool shown = ShowsMcoRoom(seen);

    if (shown)
    {
        for (const MCMcoChatLine& said : seen.Room->Chat)
        {
            chat.push_back(MCRoomChatLine{.Name = said.Name, .Text = said.Text, .Event = said.Name.empty()});
        }

        name = MCMcoApi::Printable(seen.Room->Name);
        const std::string host = MCMcoApi::Printable(seen.Room->Host);
        line = std::format(WaitingFormat, host.empty() ? SomeHost : std::string_view(host));

        for (const MCMcoApi::MCRoomPlayer& player : seen.Room->Players)
        {
            players.push_back(MCRoomRow{.Name = MCMcoApi::Printable(player.Username),
                                        .Now = std::string(player.Ready ? ReadyText : NotReadyText),
                                        .Ready = player.Ready,
                                        .Host = player.Host});
        }
    }

    const bool entered = Keep(_InRoom, shown);
    bool changed = entered;
    changed |= Keep(_RoomName, std::move(name));
    changed |= Keep(_RoomLine, std::move(line));
    changed |= Keep(_RoomPlayers, std::move(players));
    changed |= Keep(_RoomChat, std::move(chat));

    if (changed && _Model != nullptr)
    {
        _Model->Changed("in_room");
        _Model->Changed("room_name");
        _Model->Changed("room_line");
        _Model->Changed("room_players");
        _Model->Changed("room_chat");
    }

    // In or out of the room: the hint line and the focus follow (the chat's box, or the list's first game).
    if (entered)
    {
        _FocusFirstRow = true;
        ShowList();
    }
}

void MCGamesScreen::TendMcoMatch(const MCMcoSeen& seen)
{
    switch (seen.Match)
    {
        case MCMcoMatchStage::None:
        {
            _McoMatchPort = 0;
            return;
        }
        case MCMcoMatchStage::Starting:
        {
            if (!_McoWaiting && !_Join.Pending())
            {
                ShowMcoWait(std::string(MCMcoRooms::ConnectingText));
            }

            return;
        }
        case MCMcoMatchStage::JoinGame:
        {
            if (_Join.Pending() || _McoMatchPort == seen.GamePort || !seen.Room.has_value())
            {
                return;
            }

            // The game joins the guest's bridge as a launcher's game joins, under the name MCO's launcher gives it.
            _McoMatchPort = seen.GamePort;

            if (_McoWaiting)
            {
                _McoWaiting = false;
                MCRmlDialogs::Close();
            }

            Begin(MCGameJoin::MCAsk{.Where = MCGamesWhere::Mco,
                                    .Shown = MCMcoApi::Printable(seen.Room->Name),
                                    .PlayerName = seen.MatchName,
                                    .Port = seen.GamePort});
            return;
        }

        // Join-only for now: MCO's rooms are hosted by its own launcher (SPEC-mco-rooms.md).
        case MCMcoMatchStage::HostGame:
        case MCMcoMatchStage::Playing:
            return;
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
        // The remade screens stand over the main menu, logistics' screen, so that the room shown from them is a change
        // of screen, which hides them (MCLogistics::NoteTop); the room left for them hides so too.
        if (MCMenuScreens::Top() != MCMenuScreens::Main())
        {
            logistics->NoteTop(MCMenuScreens::Main());
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
