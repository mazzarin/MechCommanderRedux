#include "stdafx.h"
#include "logistics/MCRoomScreen.h"
#include "gui/MCScreenFlow.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCChatView.h"
#include "logistics/MCChooseMapWindow.h"
#include "logistics/MCMatchMaps.h"
#include "logistics/MCMatchOptionRows.h"
#include "logistics/MCMatchOptionsWindow.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCUpdateModel.h"
#include "logistics/MCUpdateScreen.h"
#include "main/MCGameSession.h"
#include "main/MCLogistics.h"
#include "network/MCGamesList.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMultiPlayer.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
    constexpr const char* RoomDocument = "room.rml";
    constexpr const char* ModelName = "room";

    constexpr std::string_view InternetPlace = "Internet";
    constexpr std::string_view LanPlace = "LAN";
    constexpr std::string_view LauncherPlace = "Multiplayer";
    constexpr std::string_view PublicAccess = "Public";
    constexpr std::string_view PrivateAccess = "Private";
    constexpr std::string_view LanAccess = "On this network";
    constexpr std::string_view InnerSphereName = "Inner Sphere";
    constexpr std::string_view ClanName = "Clan";
    constexpr std::string_view OpenPlace = "Open";
    constexpr std::string_view HostMark = " (host)";
    constexpr std::string_view ViewWord = "View";
    constexpr std::string_view ChangeWord = "Change";
    constexpr std::string_view TimeLimitFormat = "Time limit {}.";
    constexpr std::string_view NoTimeLimit = "0:00";
    constexpr std::string_view ToPlanning = "to begin planning.";
    constexpr std::string_view ToChoose = "to choose a map and the teams.";
    constexpr std::string_view ToChooseMap = "to choose a map.";
    constexpr std::string_view ToChooseTeams = "to choose the teams.";
    constexpr std::string_view AllWeaponsFormat = "All {}";
    constexpr std::string_view SomeWeaponsFormat = "{} of {}";

    /// <summary>The kit's choices of the tech base switch: Inner Sphere, then Clan.</summary>
    constexpr int InnerSphereChoice = 0;
    constexpr int ClanChoice = 1;
    /// <summary>The chat's switch: Team, then All.</summary>
    constexpr int TeamChoice = 0;
    constexpr int AllChoice = 1;

    constexpr std::array<const char*, 2> TechIds = {"tech1", "tech2"};
    constexpr std::array<const char*, 2> PointsIds = {"points1", "points2"};
    constexpr const char* SayId = "say";
    constexpr const char* ToId = "to";
    constexpr const char* ValueAttribute = "value";

    MCRoomModel& Room()
    {
        return GlobalLog()->Room;
    }

    /// <summary>The 1998 dialog's capitals in sentence case ("MOST SKILLED PILOT": "Most skilled pilot").</summary>
    std::string Sentence(std::string_view text)
    {
        return MCRmlDialogs::AsSentence(text);
    }

    /// <summary>The most points the 1998 room's RP box holds (seven digits: MCSessionScreen's InitBuffer(8)).</summary>
    constexpr int64_t MostPoints = 9'999'999;

    /// <summary>
    /// The points typed (commas and spaces skipped); none when it holds no digit or more than the 1998 RP box holds,
    /// which its tick would read back cut to seven digits.
    /// </summary>
    std::optional<int32_t> Ungrouped(std::string_view typed)
    {
        int64_t points = 0;
        bool any = false;

        for (const char c : typed)
        {
            if (c == ',' || c == ' ')
            {
                continue;
            }

            if (c < '0' || c > '9')
            {
                return std::nullopt;
            }

            points = points * 10 + (c - '0');
            any = true;

            if (points > MostPoints)
            {
                return std::nullopt;
            }
        }

        return any ? std::optional(static_cast<int32_t>(points)) : std::nullopt;
    }

    /// <summary>The option row's value's name, as it shows ("Mission's", "On").</summary>
    std::string ValueName(const MCOptionRow& row)
    {
        if (!row.Value.has_value())
        {
            return {};
        }

        const int value = row.Value->get();
        const auto found = std::ranges::find(row.Choices, value, &MCOptionChoice::Value);
        return found != row.Choices.end() ? Sentence(found->Name) : std::to_string(value);
    }
}

MCRoomScreen::MCRoomScreen()
{
    SetDocument(RoomDocument);
    Keys = {{"ENTER", "Say"}, {"ESC", "Leave"}};
    Back = [] { MCScreenFlow::Back(); };
}

MCRoomScreen::~MCRoomScreen() = default;

void MCRoomScreen::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, ModelName);
    MCRmlModel& model = *_Model;
    model.Value("eyebrow", _Eyebrow);
    model.Value("name", _Name);
    model.Value("access", _Access);
    model.Value("code", _Code);
    model.Value("count", _Count);
    model.Value("most", _Most);
    model.Value("hosting", _Hosting);
    model.Value("can_teams", _CanTeams);
    model.Value("can_forces", _CanForces);
    model.Value("tech1_name", _Tech1Name);
    model.Value("tech2_name", _Tech2Name);
    model.Value("points1", _Points1);
    model.Value("points2", _Points2);
    model.Value("full1", _Full1);
    model.Value("full2", _Full2);
    const auto slotFields = [&model](const std::string& name, std::vector<MCSlot>& slots)
    {
        model.List(name, slots, std::pair{"open", &MCSlot::Open}, std::pair{"id", &MCSlot::Id},
                   std::pair{"name", &MCSlot::Name}, std::pair{"colour", &MCSlot::Colour},
                   std::pair{"host", &MCSlot::Host}, std::pair{"me", &MCSlot::Me});
    };

    slotFields("slots1", _Slots1);
    slotFields("slots2", _Slots2);
    model.List("pool", _Pool, std::pair{"id", &MCCard::Id}, std::pair{"name", &MCCard::Name},
               std::pair{"colour", &MCCard::Colour}, std::pair{"host", &MCCard::Host}, std::pair{"me", &MCCard::Me});
    model.Value("has_map", _HasMap);
    model.Value("map_title", _MapTitle);
    model.Value("map_pic", _MapPic);
    model.Value("map_time", _MapTime);
    model.List("map_lines", _MapLines, std::pair{"text", &MCMapLine::Text});
    model.Value("options_word", _OptionsWord);
    model.List("opts", _Opts, std::pair{"name", &MCOption::Name}, std::pair{"value", &MCOption::Value},
               std::pair{"off", &MCOption::Off});
    _Chat.Bind(model, "chat");
    model.Value("can_team_chat", _CanTeamChat);
    model.Value("update_line", _UpdateLine);
    model.Value("waits", _Waits);
    model.Value("wait_name", _WaitName);
    model.Value("wait_colour", _WaitColour);
    model.Value("wait_what", _WaitWhat);
    model.Value("can_begin", _CanBegin);

    // A card's TEAM 1 and TEAM 2, and its drag onto a team's place or back to the players on no team.
    const auto argument = [](const Rml::VariantList& arguments, size_t at)
    { return arguments.size() > at ? arguments[at].Get<int>() : 0; };
    model.Event(
        "join", [this, argument](const Rml::VariantList& arguments)
        { PressJoin(static_cast<uint32_t>(argument(arguments, 0)), static_cast<int8_t>(argument(arguments, 1))); });
    model.Event("drag", [this, argument](const Rml::VariantList& arguments)
                { _Dragged = static_cast<uint32_t>(argument(arguments, 0)); });
    model.Event("drop",
                [this, argument](const Rml::VariantList& arguments)
                {
                    if (const std::optional<uint32_t> dragged = std::exchange(_Dragged, std::nullopt))
                    {
                        Drop(*dragged, static_cast<int8_t>(argument(arguments, 0)),
                             static_cast<int8_t>(argument(arguments, 1)));
                    }
                });
}

void MCRoomScreen::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("leave", [this] { PressLeave(); });
    OnClick("begin", [this] { PressBegin(); });
    OnClick("choose-map", [this] { PressChooseMap(); });
    OnClick("change-map", [this] { PressChooseMap(); });
    OnClick("view-options", [this] { PressOptions(); });
    OnClick("update", [this] { PressUpdate(); });

    for (const int8_t team : {MCRoomModel::Team1, MCRoomModel::Team2})
    {
        const size_t at = static_cast<size_t>(team - MCRoomModel::Team1);
        OnChange(TechIds[at], [this, team](int choice)
                 { PickTechBase(team, choice == ClanChoice ? MCRoomModel::Clan : MCRoomModel::InnerSphere); });
        OnText(PointsIds[at], [this, team](const std::string& typed, bool /*enter*/) { TypePoints(team, typed); });
    }

    OnText(SayId,
           [this](const std::string& typed, bool enter)
           {
               _Say = typed;

               if (!enter)
               {
                   return;
               }

               const MCOptionalRef<Rml::ElementDocument> document = Document();
               Rml::Element* to = document.has_value() ? document->get().GetElementById(ToId) : nullptr;
               Say(typed, to != nullptr && MCRmlKit::Choice(*to) == TeamChoice && _CanTeamChat);

               if (Rml::Element* box = document.has_value() ? document->get().GetElementById(SayId) : nullptr)
               {
                   box->SetAttribute(ValueAttribute, std::string());
               }

               _Say.clear();
           });
}

MCOptionalRef<Rml::Element> MCRoomScreen::FirstFocus(Rml::ElementDocument& document)
{
    // The chat's box: what a player in a room does most (the host's BEGIN PLANNING is a click or a Tab away).
    Rml::Element* say = document.GetElementById(SayId);
    return say != nullptr ? MCOptionalRef<Rml::Element>(*say) : std::nullopt;
}

void MCRoomScreen::OnShow()
{
    SDL_Log("Room: shown");
    MCRmlDialogs::SetPlace(Room().Name());
    _Dragged.reset();
    _Chat.Forget();

    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        if (Rml::Element* to = document->get().GetElementById(ToId); to != nullptr)
        {
            MCRmlKit::SetChoice(*to, AllChoice);
        }
    }

    ShowRoom();
    ShowControls();

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCRoomScreen::OnHide()
{
    SDL_Log("Room: hidden");
    _Dragged.reset();
}

void MCRoomScreen::OnDestroy()
{
    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCRoomScreen::OnFrame()
{
    // The room's model goes with logistics.
    if (GlobalLog() == nullptr)
    {
        return;
    }

    Room().Tend();
    ShowRoom();
    ShowControls();
}

template <typename T> void MCRoomScreen::Keep(const char* name, T& value, T now)
{
    if (value == now)
    {
        return;
    }

    value = std::move(now);

    if (_Model != nullptr)
    {
        _Model->Changed(name);
    }
}

void MCRoomScreen::ShowRoom()
{
    if (GlobalLog() == nullptr)
    {
        return;
    }

    const MCRoomModel& room = Room();
    const MCRoomModel::MCWhere where = room.Where();
    const std::vector<MCRoomModel::MCPlayer> players = room.Players();
    Keep("eyebrow", _Eyebrow, RoomPlace());
    Keep("name", _Name, room.Name());
    Keep("access", _Access,
         std::string(where == MCRoomModel::MCWhere::Internet ? (room.Public() ? PublicAccess : PrivateAccess)
                     : where == MCRoomModel::MCWhere::Lan    ? LanAccess
                                                             : std::string_view()));
    Keep("code", _Code, where == MCRoomModel::MCWhere::Internet ? room.Code() : std::string());
    Keep("count", _Count, static_cast<int>(players.size()));
    Keep("most", _Most, static_cast<int>(room.Most()));
    Keep("hosting", _Hosting, room.Hosting());
    Keep("can_teams", _CanTeams, room.CanChangeTeams());
    Keep("can_forces", _CanForces, room.CanChangeForces());
    ShowTeams(players);
    ShowMatch();
    ShowChat();
    ShowFoot(players);
}

void MCRoomScreen::ShowTeams(const std::vector<MCRoomModel::MCPlayer>& players)
{
    const MCRoomModel& room = Room();
    const auto techName = [&room](int8_t team)
    { return std::string(room.TechBase(team) == MCRoomModel::Clan ? ClanName : InnerSphereName); };
    Keep("tech1_name", _Tech1Name, techName(MCRoomModel::Team1));
    Keep("tech2_name", _Tech2Name, techName(MCRoomModel::Team2));

    // A box being typed in keeps what is typed (the host's), else the points as they stand.
    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        Rml::Element* focused =
            document->get().GetContext() != nullptr ? document->get().GetContext()->GetFocusElement() : nullptr;
        const auto typing = [&document, focused](size_t at)
        { return focused != nullptr && focused == document->get().GetElementById(PointsIds[at]); };

        if (!typing(0) || !_Hosting)
        {
            Keep("points1", _Points1, MCGroupedPoints(room.Points(MCRoomModel::Team1)));
        }

        if (!typing(1) || !_Hosting)
        {
            Keep("points2", _Points2, MCGroupedPoints(room.Points(MCRoomModel::Team2)));
        }
    }

    std::array<std::vector<MCSlot>, 2> slots;
    std::vector<MCCard> pool;

    for (std::vector<MCSlot>& team : slots)
    {
        team.resize(MCRoomModel::TeamSlots);
    }

    for (const MCRoomModel::MCPlayer& player : players)
    {
        const std::string colour = MCChatView::ColourOf(player.Number);

        if (player.Team == MCRoomModel::Team1 || player.Team == MCRoomModel::Team2)
        {
            std::vector<MCSlot>& team = slots[static_cast<size_t>(player.Team - MCRoomModel::Team1)];

            if (player.Slot >= 0 && static_cast<size_t>(player.Slot) < team.size())
            {
                team[static_cast<size_t>(player.Slot)] = MCSlot{.Open = false,
                                                                .Id = static_cast<int>(player.Id),
                                                                .Name = player.Name,
                                                                .Colour = colour,
                                                                .Host = player.Host,
                                                                .Me = player.Me};
            }

            continue;
        }

        pool.push_back(MCCard{.Id = static_cast<int>(player.Id),
                              .Name = player.Name,
                              .Colour = colour,
                              .Host = player.Host,
                              .Me = player.Me});
    }

    const auto full = [](const std::vector<MCSlot>& team) { return std::ranges::none_of(team, &MCSlot::Open); };
    Keep("full1", _Full1, full(slots[0]));
    Keep("full2", _Full2, full(slots[1]));
    Keep("slots1", _Slots1, std::move(slots[0]));
    Keep("slots2", _Slots2, std::move(slots[1]));
    Keep("pool", _Pool, std::move(pool));
}

void MCRoomScreen::ShowMatch()
{
    // The host's mission, or a guest's as its host named it (a guest has no file of the room's).
    const MCRoomModel& room = Room();
    const bool hasMap = room.HasMission();
    Keep("has_map", _HasMap, hasMap);
    Keep("map_title", _MapTitle, hasMap ? room.MissionName : std::string());
    Keep("map_pic", _MapPic,
         hasMap && !room.MapFile.empty() ? std::format("{}{}", MCRmlArt::TerrainScheme, room.MapFile) : std::string());
    Keep("map_time", _MapTime,
         hasMap && !room.MissionTime.empty() && room.MissionTime != NoTimeLimit
             ? std::format(TimeLimitFormat, room.MissionTime)
             : std::string());

    // The mission's description as one paragraph, as the room's frames have it (CHOOSE MAP shows its paragraphs).
    std::vector<MCMapLine> lines;
    std::string text;

    for (const std::string& paragraph : hasMap ? MCParagraphs(room.Description) : std::vector<std::string>{})
    {
        text += text.empty() ? paragraph : " " + paragraph;
    }

    if (!text.empty())
    {
        lines.push_back(MCMapLine{std::move(text)});
    }

    Keep("map_lines", _MapLines, std::move(lines));
    Keep("options_word", _OptionsWord, std::string(room.CanChangeTeams() ? ChangeWord : ViewWord));

    // The options as the match plays them (MATCH OPTIONS' rows), the most skilled pilot first as the room's frames
    // have it; the weapons as how many families are on. An option held (a player's game lacks it) shows grey.
    ReadMatchOptions();
    const std::span<const MCOptionRow> rows = MatchOptionRows();
    const std::string_view pilot = MCCapabilityOf(MCMatchOption::MostSkilledPilot).Name;
    std::vector<MCOption> options;
    int headings = 0;
    int weapons = 0;
    int weaponsOn = 0;
    int32_t weaponsRow = -1;

    for (size_t at = 0; at < rows.size(); ++at)
    {
        const MCOptionRow& row = rows[at];

        if (row.IsHeading())
        {
            headings++;

            if (headings > 1)
            {
                weaponsRow = static_cast<int32_t>(at);
            }

            continue;
        }

        if (headings > 1)
        {
            weapons++;
            weaponsOn += row.Value.has_value() && row.Value->get() != 0 ? 1 : 0;
            continue;
        }

        MCOption option{.Name = Sentence(row.Label),
                        .Value = ValueName(row),
                        .Off = !MatchOptionHeld(static_cast<int32_t>(at)).empty()};
        options.insert(row.Label == pilot ? options.begin() : options.end(), std::move(option));
    }

    if (weaponsRow >= 0)
    {
        options.push_back(MCOption{.Name = Sentence(rows[static_cast<size_t>(weaponsRow)].Label),
                                   .Value = weaponsOn == weapons ? std::format(AllWeaponsFormat, weapons)
                                                                 : std::format(SomeWeaponsFormat, weaponsOn, weapons),
                                   .Off = !MatchOptionHeld(weaponsRow).empty()});
    }

    Keep("opts", _Opts, std::move(options));
}

void MCRoomScreen::ShowChat()
{
    if (_Model != nullptr)
    {
        _Chat.Show(*_Model);
    }
}

void MCRoomScreen::ShowFoot(const std::vector<MCRoomModel::MCPlayer>& players)
{
    const MCRoomModel& room = Room();
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const auto me = std::ranges::find(players, true, &MCRoomModel::MCPlayer::Me);
    Keep("can_team_chat", _CanTeamChat, me != players.end() && me->Team != MCRoomModel::NoTeam);

    // Whom the start waits for (guests away updating), else the update waiting for a launcher's game.
    // Its names as typed ("Waiting for Ann to rejoin."), not the 1998 room's capitals.
    const std::string waitsLine = MCLobbyUpdate::StartWaitsLine();
    Keep("waits", _Waits,
         room.StartWaits.empty() ? std::string()
         : !waitsLine.empty()    ? waitsLine
                                 : Sentence(room.StartWaits));
    Keep("update_line", _UpdateLine,
         room.StartWaits.empty() && MCLobbyUpdate::InRoom() ? Sentence(UpdateLineText()) : std::string());

    // A guest waits for the host: to choose a map and the teams, or whichever is left, then to begin planning.
    const auto host = std::ranges::find(players, true, &MCRoomModel::MCPlayer::Host);
    const bool hasMap = room.HasMission();
    const bool placed = !players.empty() && std::ranges::all_of(players, [](const MCRoomModel::MCPlayer& player)
                                                                { return player.Team != MCRoomModel::NoTeam; });
    Keep("wait_name", _WaitName, host != players.end() && !room.Hosting() ? host->Name : std::string());
    Keep("wait_colour", _WaitColour, host != players.end() ? MCChatView::ColourOf(host->Number) : std::string());
    Keep("wait_what", _WaitWhat,
         std::string(hasMap && placed ? ToPlanning
                     : hasMap         ? ToChooseTeams
                     : placed         ? ToChooseMap
                                      : ToChoose));
    Keep("can_begin", _CanBegin, room.Hosting() && room.CanStart && game != nullptr);
}

void MCRoomScreen::ShowControls()
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value() || GlobalLog() == nullptr)
    {
        return;
    }

    const MCRoomModel& room = Room();

    for (const int8_t team : {MCRoomModel::Team1, MCRoomModel::Team2})
    {
        const size_t at = static_cast<size_t>(team - MCRoomModel::Team1);

        if (Rml::Element* tech = document->get().GetElementById(TechIds[at]); tech != nullptr)
        {
            const int choice = room.TechBase(team) == MCRoomModel::Clan ? ClanChoice : InnerSphereChoice;

            if (MCRmlKit::Choice(*tech) != choice)
            {
                MCRmlKit::SetChoice(*tech, choice);
            }
        }

        Rml::Element* points = document->get().GetElementById(PointsIds[at]);
        const std::string& shown = at == 0 ? _Points1 : _Points2;
        Rml::Context* context = document->get().GetContext();

        if (points != nullptr && (context == nullptr || context->GetFocusElement() != points) &&
            points->GetAttribute<Rml::String>(ValueAttribute, "") != shown)
        {
            points->SetAttribute(ValueAttribute, shown);
        }
    }

    // Off a team the chat goes to all (its TEAM held): the switch says so, though it was on TEAM before.
    if (Rml::Element* to = document->get().GetElementById(ToId);
        to != nullptr && !_CanTeamChat && MCRmlKit::Choice(*to) == TeamChoice)
    {
        MCRmlKit::SetChoice(*to, AllChoice);
    }
}

MCRoomScreen::MCSeen MCRoomScreen::Seen() const
{
    MCSeen seen{.Eyebrow = _Eyebrow,
                .Name = _Name,
                .Tech1 = _Tech1Name,
                .Tech2 = _Tech2Name,
                .Points1 = _Points1,
                .Points2 = _Points2,
                .Map = _HasMap ? _MapTitle : std::string(),
                .MapPicture = _HasMap ? _MapPic : std::string(),
                .MapTime = _HasMap ? _MapTime : std::string(),
                .UpdateLine = _UpdateLine,
                .Hosting = _Hosting,
                .CanBegin = _CanBegin};
    std::vector<std::string> facts;

    if (!_Access.empty())
    {
        facts.push_back(_Access);
    }

    if (!_Code.empty())
    {
        facts.push_back(std::format("Code {}", _Code));
    }

    facts.push_back(std::format("{} of {} players", _Count, _Most));

    for (const std::string& fact : facts)
    {
        seen.Facts += seen.Facts.empty() ? fact : " | " + fact;
    }

    const auto places = [](const std::vector<MCSlot>& slots)
    {
        std::vector<std::string> shown;

        for (const MCSlot& slot : slots)
        {
            shown.push_back(slot.Open ? std::string(OpenPlace)
                                      : slot.Name + std::string(slot.Host ? HostMark : std::string_view()));
        }

        return shown;
    };

    seen.Team1 = places(_Slots1);
    seen.Team2 = places(_Slots2);

    for (const MCCard& card : _Pool)
    {
        seen.Pool.push_back(card.Name);
    }

    for (const MCMapLine& line : _MapLines)
    {
        seen.MapText.push_back(line.Text);
    }

    for (const MCOption& option : _Opts)
    {
        seen.Options.push_back(std::format("{} | {}", option.Name, option.Value));
    }

    for (const MCChatView::MCLine& line : _Chat.Lines())
    {
        seen.Chat.push_back(line.Game ? line.Text : std::format("{}: {}", line.Name, line.Text));
    }

    seen.Status = !_Waits.empty()                   ? _Waits
                  : !_UpdateLine.empty()            ? _UpdateLine
                  : !_Hosting && !_WaitName.empty() ? std::format("Waiting for {} {}", _WaitName, _WaitWhat)
                                                    : std::string();
    return seen;
}

void MCRoomScreen::PressBegin()
{
    if (_CanBegin && GlobalLog() != nullptr)
    {
        Room().BeginPlanning();
    }
}

void MCRoomScreen::PressLeave()
{
    if (Back)
    {
        Back();
    }
}

void MCRoomScreen::PressJoin(uint32_t playerId, int8_t team)
{
    Room().JoinTeam(playerId, team);
}

void MCRoomScreen::Drop(uint32_t playerId, int8_t team, int8_t slot)
{
    MCRoomModel& room = Room();

    if (team != MCRoomModel::Team1 && team != MCRoomModel::Team2)
    {
        room.Assign(playerId, MCRoomModel::NoTeam, 0);
        return;
    }

    // A place taken: the card goes to the team's first open one, as TEAM 1 and TEAM 2 send it.
    const std::array<uint32_t, MCRoomModel::TeamSlots>& placed =
        team == MCRoomModel::Team1 ? room.Team1Players : room.Team2Players;

    if (slot >= 0 && static_cast<size_t>(slot) < placed.size() &&
        placed[static_cast<size_t>(slot)] == MCRoomModel::NoPlayer)
    {
        room.Assign(playerId, team, slot);
        return;
    }

    room.JoinTeam(playerId, team);
}

void MCRoomScreen::TypePoints(int8_t team, std::string_view typed)
{
    if (const std::optional<int32_t> points = Ungrouped(typed); points.has_value())
    {
        Room().SetPoints(team, *points);
    }
}

void MCRoomScreen::PickTechBase(int8_t team, int8_t techBase)
{
    Room().SetTechBase(team, techBase);
    // The switch follows the model (back where it was when the host can't change it now).
    ShowControls();
}

void MCRoomScreen::Say(std::string_view text, bool toTeam)
{
    MCChatView::Say(text, toTeam);
}

void MCRoomScreen::PressChooseMap()
{
    if (!Room().CanChangeTeams())
    {
        return;
    }

    // CHOOSE MAP's window, the room's map picked; its CHOOSE loads the map into the room (1998's LOAD MAP). Without the
    // window, the 1998 mission list (the flow's "mission pick").
    const bool shown = ShowChooseMapWindow(Room().LoadedMission,
                                           [](const std::string& file)
                                           {
                                               if (GlobalLog() != nullptr)
                                               {
                                                   GlobalLog()->Room.ChooseMission(file);
                                               }
                                           });

    if (!shown)
    {
        MCScreenFlow::Open(MCMenuScreens::MissionPick());
    }
}

void MCRoomScreen::PressOptions()
{
    ShowMatchOptionsWindow();
}

void MCRoomScreen::PressUpdate()
{
    // Stage 6's UPDATE through the screens' flow over the room (BACK brings the room back as it stands); the 1998 page
    // over the 1998 room when the menus' RmlUi can't start.
    if (MCUpdateScreen::Open())
    {
        return;
    }

    if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr && logistics->SessionScreen)
    {
        logistics->SessionScreen->ShowUpdateOver();
    }
}

// ---- in the 1998 room's place ----

namespace
{
    MCGuiHandle<MCRoomScreen>& Made()
    {
        static MCGuiHandle<MCRoomScreen> made;
        return made;
    }
}

bool ShowRoomScreen()
{
    if (!Made())
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return false;
        }

        Made() = menus->get().Menus().CreateChild<MCRoomScreen>([](MCRoomScreen&) {});
    }

    MCRoomScreen& room = *Made();

    if (!room.Document().has_value())
    {
        for (const MCScreenChange::MCPiece& piece : room.Prepare())
        {
            piece.Run();
        }
    }

    if (!room.Error().empty())
    {
        SDL_Log("Room: the remade room can't show: %s", room.Error().c_str());
        return false;
    }

    room.Show();
    return true;
}

void HideRoomScreen()
{
    // MATCH OPTIONS' and CHOOSE MAP's windows go with the room they are over.
    if (MatchOptionsWindowShown())
    {
        MatchOptionsWindow()->Hide();
    }

    if (ChooseMapWindowShown())
    {
        ChooseMapWindow()->Hide();
    }

    if (Made() && Made()->Shown())
    {
        Made()->Hide();
    }
}

std::string RoomPlace()
{
    const MCRoomModel::MCWhere where =
        GlobalLog() != nullptr ? GlobalLog()->Room.Where() : MCRoomModel::MCWhere::Launcher;
    return std::string(where == MCRoomModel::MCWhere::Internet ? InternetPlace
                       : where == MCRoomModel::MCWhere::Lan    ? LanPlace
                                                               : LauncherPlace);
}

MCGuiHandle<MCRoomScreen> RoomScreen()
{
    return Made();
}

bool RoomScreenShown()
{
    return Made() && Made()->Shown();
}

bool RoomScreenOnTop()
{
    if (!RoomScreenShown())
    {
        return false;
    }

    const MCOptionalRef<MCRmlScreen> top = Made()->Layer().Top();
    return top.has_value() && Made().Is(top->get());
}

// ---- a launcher's way in ----

namespace
{
    constexpr const char* LauncherDocument = "launcher.rml";
    /// <summary>The room a bare <c>-join</c> joins, while its name isn't known.</summary>
    constexpr std::string_view SomeGame = "the game";
    constexpr std::string_view CancelWord = "Cancel";

    MCGuiHandle<MCLauncherScreen>& Launcher()
    {
        static MCGuiHandle<MCLauncherScreen> made;
        return made;
    }

    [[noreturn]] void EndTheGame()
    {
        KillTheGame();
    }
}

MCLauncherScreen::MCLauncherScreen()
{
    SetDocument(LauncherDocument);
    Keys = {{"ESC", "Cancel"}};
    Back = [] { EndTheGame(); };
}

std::string MCLauncherScreen::JoinLine()
{
    if (LobbyLaunchArgs.IsHost)
    {
        return std::string(MCGameJoin::HostingText);
    }

    // The launcher's -session names it; a bare -join learns it once the session is found.
    std::string name = LobbyLaunchArgs.SessionName;

    if (name.empty() && GlobalLog() != nullptr)
    {
        name = GlobalLog()->Room.Name();
    }

    return std::format(MCGameJoin::JoiningFormat, name.empty() ? SomeGame : std::string_view(name));
}

void MCLauncherScreen::OnShow()
{
    SDL_Log("Launcher: joining shown");
    _Said.clear();

    // A say the change of screens hid (the host's update wait, "Bob closed the game.") comes back over the painting
    // (MCRmlDialogs::Tend) in place of the join's.
    if (!MCRmlDialogs::Returning())
    {
        Say();
    }
}

void MCLauncherScreen::OnFrame()
{
    const MCGuiHandle<MCRmlDialog> dialog = MCRmlDialogs::Dialog();

    if (!dialog || MCRmlDialogs::Returning())
    {
        return;
    }

    // Before the game's first room, the join's dialog says what the link's wait would ("Connecting...") and stays up
    // through the link's own: its line again when the room's name is found, or when that wait ended it.
    const bool roomSeen = static_cast<bool>(RoomScreen());

    if (!MCRmlDialogs::Shown())
    {
        if (!roomSeen)
        {
            Say();
        }

        return;
    }

    const MCRmlDialog::MCSay& said = dialog->Said();
    const bool wait = said.Main.empty() && said.Other.empty();

    if (wait && !roomSeen)
    {
        Say();
        return;
    }

    // A later wait (the host restarting into an update) with the join's way out: CANCEL ends the game, as the 1998
    // waiting screen's button did. It stays a wait, which ends as it would have.
    if (wait)
    {
        MCRmlDialog::MCSay cancellable = said;
        cancellable.Other = std::string(CancelWord);
        cancellable.OnOther = [] { EndTheGame(); };
        cancellable.Wait = true;
        MCRmlDialogs::Show(std::move(cancellable));
        return;
    }

    // Its own join's line, once the room's name is found.
    if (!_Said.empty() && said.Heading == _Said && JoinLine() != _Said)
    {
        Say();
    }
}

void MCLauncherScreen::Say()
{
    _Said = JoinLine();
    MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = std::string(LauncherPlace),
                                          .Heading = _Said,
                                          .Main = {},
                                          .Other = std::string(CancelWord),
                                          .OnOther = [] { EndTheGame(); }});
}

bool ShowLauncherScreen()
{
    if (!Launcher())
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return false;
        }

        Launcher() = menus->get().Menus().CreateChild<MCLauncherScreen>([](MCLauncherScreen&) {});
    }

    MCLauncherScreen& launcher = *Launcher();

    if (!launcher.Document().has_value())
    {
        for (const MCScreenChange::MCPiece& piece : launcher.Prepare())
        {
            piece.Run();
        }
    }

    if (!launcher.Error().empty())
    {
        SDL_Log("Launcher: the join can't show on the painting: %s", launcher.Error().c_str());
        return false;
    }

    launcher.Show();
    return true;
}

bool LauncherScreenShown()
{
    return Launcher() && Launcher()->Shown();
}
