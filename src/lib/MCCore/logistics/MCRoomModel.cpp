#include "stdafx.h"
#include "logistics/MCRoomModel.h"

#include "gui/MCGuiEvent.h"
#include "gui/MCGuiObject.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "lib/MCReleaseData.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "network/MCInternetGame.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMPGameConfiguration.h"
#include "network/MCMcoRooms.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "network/MCMcoSeats.h"
#include "network/MCRoomEnd.h"
#include "sound/MCSoundSystem.h"

/// <summary>The GUI object the room's tick's timer goes to (the 1998 session screen's timer, as it was).</summary>
class MCRoomTicker final : public MCGuiObject
{
public:
    void HandleEvent(MCGuiEvent& event) override
    {
        if (event.Type != MCGuiEventType::Timer)
        {
            MCGuiObject::HandleEvent(event);
            return;
        }

        if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
        {
            logistics->Room.Tick();
        }
    }
};

namespace
{
    /// <summary>The room's tick's timer (the 1998 screen's), and how often it ticks.</summary>
    constexpr int16_t TickTimer = 0;
    constexpr int32_t TickMs = 500;
    /// <summary>How long (the game clock) a room pings after it opens, so the latencies are known.</summary>
    constexpr uint32_t PingMs = 3000;
    /// <summary>
    /// Port-only (the personal build): how long (the game clock) the host waits for a player who joined to ping (its
    /// game up) before it shows that player the room anyway, its pings lost on the way.
    /// </summary>
    constexpr uint32_t ShowAnywayMs = 5000;
    /// <summary>The characters the 1998 room's points' box holds (its 8-byte buffer, the last a NUL).</summary>
    constexpr size_t PointsBoxChars = 7;
    /// <summary>The description box's fonts: the mission's, and the check-in's wait.</summary>
    constexpr int32_t DescriptionFont = 0;
    constexpr int32_t CheckInFont = 2;
    /// <summary>The players' lights' states: on no team (or checked in), on a team, checking in.</summary>
    constexpr int32_t LightIdle = 0;
    constexpr int32_t LightOnTeam = 1;
    constexpr int32_t LightCheckingIn = 2;
    /// <summary>Betty's "launch" as the match starts.</summary>
    constexpr uint32_t StartSample = 0x19;

    /// <summary>Port: names the players whose game data differ from the host's (as good as without the mission).</summary>
    constexpr std::string_view DiffersText = "These players' game data differs from yours: {}";
    /// <summary>
    /// Port-only (Redux HD): names the files this game's copy of the release's data lacks (lib/MCReleaseData.h): the
    /// game folder's were changed, so its data may be what differs.
    /// </summary>
    constexpr std::string_view LackingText = "Your copy of {} isn't the release's.";
    constexpr std::string_view LackingSeparator = ", ";
    /// <summary>Port: names the players whose builds can't check their game data (older ones), who play.</summary>
    constexpr std::string_view UncheckedText = "Can't check these players' game data: {}. Tell them to upgrade.";

    MCRoomModel& Room()
    {
        return GlobalLog()->Room;
    }

    /// <summary>Whether logistics' screen is the room (the 1998 session screen, in the remade room's place too).</summary>
    bool RoomCurrent()
    {
        return MCMenuScreens::Top() == MCMenuScreens::Room();
    }

    /// <summary>The slot of <paramref name="playerId"/> on <paramref name="team"/>, or -1.</summary>
    int32_t FindSlot(std::span<const uint32_t> team, uint32_t playerId)
    {
        const auto found = std::ranges::find(team, playerId);
        return found == team.end() ? -1 : static_cast<int32_t>(found - team.begin());
    }

    /// <summary>The number in a resource point box's text, as the original's <c>atol</c> read it.</summary>
    int32_t ReadPoints(const std::string& text)
    {
        return std::atol(text.c_str());
    }

    /// <summary>
    /// Sends a guaranteed message carrying a file name (<paramref name="type"/> load mission or start) to every
    /// player.
    /// </summary>
    void SendFileName(uint16_t type, std::string_view fileName)
    {
        // The header, then the name and its NUL.
        constexpr size_t headerSize = sizeof(MCMPFileNameMessage);
        MCMPFileNameMessage header{};
        header.Tagger.Clear();
        header.Header = type;
        std::vector<uint8_t> buffer(headerSize + fileName.size() + 1, 0);
        std::memcpy(buffer.data(), &header, headerSize);
        std::ranges::copy(fileName, buffer.begin() + headerSize);
        MultiPlayer()->SessionManager->SendMessageToGroup(0, buffer);
    }

    /// <summary>Sends a two-long guaranteed message to every player.</summary>
    void SendTwoLongs(uint16_t type, int32_t value1, int32_t value2)
    {
        MCMPTwoLongMessage message{};
        message.Header = type;
        message.Value1 = value1;
        message.Value2 = value2;
        MultiPlayer()->SessionManager->SendMessageToGroup(0, LinkupBytes(message));
    }

    /// <summary>The file name part of <paramref name="path"/> without folder or extension (<c>_splitpath</c>'s fname).</summary>
    std::string SplitFileName(std::string_view path)
    {
        const size_t slash = path.find_last_of("\\/:");
        std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
        const size_t dot = name.rfind('.');
        return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
    }

    /// <summary>Shows <paramref name="text"/> in the message dialog with one button running <paramref name="onOk"/>.</summary>
    void ShowDialog(std::string_view text, std::function<void()> onOk, std::string_view upArt, std::string_view downArt,
                    std::function<void(int32_t)> onResult)
    {
        MCMenuDialog& dialog = *GlobalLog()->Message;
        dialog.SetText(text);
        dialog.SetTwoButton(false);
        dialog.Ok.Exec = std::move(onOk);
        dialog.Ok.SetPictures(upArt, downArt);
        dialog.Ok.Disabled = false;
        dialog.Callback = std::move(onResult);
        dialog.Activate();
    }

    /// <summary>The dialog's Cancel: drops the loaded mission.</summary>
    void MPCancel()
    {
        GlobalLog()->Message->Callback = nullptr;
        Room().CancelMission();
    }

    /// <summary>After a multiplayer save: returns to the room and tells the others which file to load.</summary>
    void MPLoadWorked(int32_t)
    {
        // The 1998 room's mission pick (its load screen) put back, the room shown again through the flow.
        Room().LeftMissionPick();

        // Port-only (the personal build): a player who joined meanwhile wasn't asked for the file: the room is shown
        // again first, which asks everyone (MCRoomModel::WatchRoom).
        if (!Room().AllShown())
        {
            return;
        }

        SendFileName(GuaranteedHeader(MCMPMessageType::LoadMission), Room().MissionFile);
        // Port-only (the personal build): then the points as the host has them: a guest's load sets the mission's own,
        // and the host may have changed them since (or set them again as it showed the room again).
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::RPUpdate), Room().Team1Points, 1);
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::RPUpdate), Room().Team2Points, 2);
    }

    /// <summary>Tells every player to start the loaded mission.</summary>
    void StartMissionNow()
    {
        std::string missionName = SplitFileName(Room().MissionFile);
        // Port-only (the personal build): the room takes players until here (the original locked it at the ready room's
        // GO).
        MultiPlayer()->SessionManager->LockSession();
        SendFileName(GuaranteedHeader(MCMPMessageType::Start), missionName);
        Room().StopTick();
        MultiPlayer()->SessionManager->SendLatencyInfo();
        SoundSystem()->PlayBettySample(StartSample);
        // Port-only (the personal build): a guest still away updating leaves its team first (network/MCLobbyUpdate.h).
        MCLobbyUpdate::MatchStarts();
        Room().PublishMatchState();

        if (!GlobalLog()->LoadMatchMission(missionName))
        {
            LeaveUnloadedMatch();
        }
    }

    /// <summary>
    /// The start: starts the loaded mission. Port-only (the personal build): while a guest is away updating, it asks
    /// first, and OK starts without it if the match can still start (network/MCLobbyUpdate.h).
    /// </summary>
    void StartMission()
    {
        // Port-only (the personal build): a player who joined since the start lit is shown the room first.
        Room().CheckGoodToGo();

        if (!Room().CanStart)
        {
            return;
        }

        const std::string question = MCLobbyUpdate::StartQuestion();

        if (question.empty())
        {
            StartMissionNow();
            return;
        }

        AskMenuQuestion(
            *GlobalLog()->Question, question,
            []
            {
                // Still the room, and the match still able to start (players may have left while it asked).
                if (!RoomCurrent())
                {
                    return;
                }

                Room().CheckGoodToGo();

                if (Room().CanStart)
                {
                    StartMissionNow();
                }
            },
            nullptr, "bg_cancl.tga", true);
    }

    /// <summary>The session's manager, in a multiplayer game.</summary>
    MCOptionalRef<MCSessionManager> Manager()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

        if (game == nullptr || game->SessionManager == nullptr)
        {
            return std::nullopt;
        }

        return *game->SessionManager;
    }

    /// <summary>The slot of <paramref name="playerId"/> on <paramref name="team"/>, if it's there.</summary>
    std::optional<int8_t> SlotOf(std::span<const uint32_t> team, uint32_t playerId)
    {
        const auto found = std::ranges::find(team, playerId);
        return found == team.end() ? std::nullopt : std::optional(static_cast<int8_t>(found - team.begin()));
    }

    /// <summary>Whether <paramref name="team"/> is one of the two teams.</summary>
    bool IsTeam(int8_t team)
    {
        return team == MCRoomModel::Team1 || team == MCRoomModel::Team2;
    }
}

auto MCRoomModel::Players() const -> std::vector<MCPlayer>
{
    const MCOptionalRef<MCSessionManager> manager = Manager();

    if (!manager.has_value() || !Made())
    {
        return {};
    }

    // The players with a name on the room (the 1998 screen's name slots, by id), the session's names and numbers.
    std::vector<MCPlayer> players;

    for (const MCSeat& seat : Seats)
    {
        if (seat.Id == NoPlayer)
        {
            continue;
        }

        const std::shared_ptr<MCFidpPlayer> session = manager->get().GetPlayer(seat.Id);

        if (session == nullptr)
        {
            continue;
        }

        MCPlayer player{.Id = session->Id,
                        .Name = session->Name,
                        .Number = session->PlayerNumber,
                        .Host = session->Id == manager->get().ServerID,
                        .Me = session->Id == manager->get().MyPlayerID};

        if (const std::optional<int8_t> slot = SlotOf(Team1Players, player.Id); slot.has_value())
        {
            player.Team = Team1;
            player.Slot = *slot;
        }
        else if (const std::optional<int8_t> slot2 = SlotOf(Team2Players, player.Id); slot2.has_value())
        {
            player.Team = Team2;
            player.Slot = *slot2;
        }

        players.push_back(std::move(player));
    }

    return players;
}

auto MCRoomModel::Hosting() const -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return game != nullptr && game->IsHost != 0;
}

auto MCRoomModel::HostName() const -> std::string
{
    const MCOptionalRef<MCSessionManager> manager = Manager();

    if (!manager.has_value())
    {
        return {};
    }

    const std::shared_ptr<MCFidpPlayer> host = manager->get().GetPlayer(manager->get().ServerID);
    return host != nullptr ? host->Name : std::string();
}

auto MCRoomModel::Name() const -> std::string
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    const std::shared_ptr<MCFidpSession> session = manager.has_value() ? manager->get().CurrentSession() : nullptr;
    return session != nullptr ? session->Name : std::string();
}

auto MCRoomModel::Most() const -> int32_t
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    const std::shared_ptr<MCFidpSession> session = manager.has_value() ? manager->get().CurrentSession() : nullptr;
    return session != nullptr ? static_cast<int32_t>(session->SessionDesc.dwMaxPlayers)
                              : static_cast<int32_t>(MaxPlayers);
}

auto MCRoomModel::Where() const -> MCWhere
{
    if (LaunchedFromLobby)
    {
        return MCWhere::Launcher;
    }

    // MCO's match is an internet game, listed in MCO's lobby as INTERNET lists it, though its link is the bridge's.
    if (MCMcoRooms::InMatch())
    {
        return MCWhere::Internet;
    }

    const MCOptionalRef<MCSessionManager> manager = Manager();
    return manager.has_value() && manager->get().Internet != nullptr ? MCWhere::Internet : MCWhere::Lan;
}

auto MCRoomModel::Public() const -> bool
{
    if (MCMcoRooms::InMatch())
    {
        return true;
    }

    const MCOptionalRef<MCSessionManager> manager = Manager();
    return manager.has_value() && manager->get().Internet != nullptr && manager->get().Internet->Public();
}

auto MCRoomModel::Code() const -> std::string
{
    const MCOptionalRef<MCSessionManager> manager = Manager();
    return manager.has_value() && manager->get().Internet != nullptr ? manager->get().Internet->Code() : std::string();
}

auto MCRoomModel::Points(int8_t team) const -> int32_t
{
    return team == Team1 ? Team1Points : team == Team2 ? Team2Points : 0;
}

auto MCRoomModel::TechBase(int8_t team) const -> int8_t
{
    return team == Team1 ? Team1TechBase : team == Team2 ? Team2TechBase : InnerSphere;
}

auto MCRoomModel::HasMission() const -> bool
{
    return Hosting() ? !MissionFile.empty() : !LoadedMission.empty();
}

auto MCRoomModel::Assign(uint32_t playerId, int8_t team, int8_t slot) -> void
{
    if (Made() && CanChangeTeams())
    {
        AssignPlayer(playerId, IsTeam(team) ? team : NoTeam, IsTeam(team) ? slot : 0, false);
    }
}

auto MCRoomModel::JoinTeam(uint32_t playerId, int8_t team) -> void
{
    if (!IsTeam(team))
    {
        Assign(playerId, NoTeam, 0);
        return;
    }

    const std::array<uint32_t, TeamSlots>& players = team == Team1 ? Team1Players : Team2Players;

    // Already on it: it stays where it is.
    if (SlotOf(players, playerId).has_value())
    {
        return;
    }

    if (const std::optional<int8_t> open = SlotOf(players, NoPlayer); open.has_value())
    {
        Assign(playerId, team, *open);
    }
}

auto MCRoomModel::SetTechBase(int8_t team, int8_t techBase) -> void
{
    if (Made() && CanChangeForces() && IsTeam(team) && (techBase == InnerSphere || techBase == Clan))
    {
        PickTechBase(team, techBase);
    }
}

auto MCRoomModel::SetPoints(int8_t team, int32_t points) -> void
{
    if (!Made() || !CanChangeForces())
    {
        return;
    }

    if (team == Team1)
    {
        SetTeam1RP(std::max(points, 0));
    }
    else if (team == Team2)
    {
        SetTeam2RP(std::max(points, 0));
    }
}

auto MCRoomModel::ChooseMission(std::string_view mission) -> void
{
    if (Made() && CanChangeTeams())
    {
        LoadMission(mission);
    }
}

auto MCRoomModel::BeginPlanning() -> void
{
    if (Made() && Hosting() && CanStart)
    {
        Begin();
    }
}

auto MCRoomModel::Tend() -> void
{
    if (const std::shared_ptr<MCMultiPlayer> game = MultiPlayer(); game != nullptr)
    {
        game->UpdateMatchOptions();
    }
}

auto MCRoomModel::Seat(size_t seat, uint32_t playerId) -> void
{
    MCSeat& seated = Seats.at(seat);
    seated.Id = playerId;
    const MCOptionalRef<MCSessionManager> manager = Manager();

    if (const std::shared_ptr<MCFidpPlayer> player =
            manager.has_value() && playerId != NoPlayer ? manager->get().GetPlayer(playerId) : nullptr;
        player != nullptr)
    {
        seated.Name = player->Name;
    }
}

auto MCRoomModel::SeatOf(uint32_t playerId) -> MCOptionalRef<MCSeat>
{
    const auto found = std::ranges::find(Seats, playerId, &MCSeat::Id);
    return found != Seats.end() && playerId != NoPlayer ? MCOptionalRef<MCSeat>(*found) : std::nullopt;
}

auto MCRoomModel::ShowsName(uint32_t playerId) const -> bool
{
    return playerId != NoPlayer &&
           std::ranges::any_of(Seats, [playerId](const MCSeat& seat) { return seat.Shown && seat.Id == playerId; });
}

auto MCRoomModel::ShowSeat(size_t seat, bool shown) -> void
{
    Seats.at(seat).Shown = shown;
    ToView([seat, shown](MCRoomView& view) { view.ShowName(seat, shown); });
}

auto MCRoomModel::AskingFiles() const -> bool
{
    return std::ranges::any_of(Seats, [](const MCSeat& seat) { return seat.Files == MCFileStatus::Waiting; });
}

MCRoomModel::MCRoomModel() = default;

MCRoomModel::~MCRoomModel() = default;

template <typename F> void MCRoomModel::ToView(F&& tell) const
{
    if (const std::shared_ptr<MCRoomView> view = _View.lock(); view != nullptr)
    {
        std::forward<F>(tell)(*view);
    }
}

auto MCRoomModel::SetView(std::weak_ptr<MCRoomView> view) -> void
{
    _View = std::move(view);
}

auto MCRoomModel::SetMade(bool made) -> void
{
    _Made = made;
}

auto MCRoomModel::Made() const -> bool
{
    return _Made;
}

auto MCRoomModel::LeftMissionPick() const -> void
{
    ToView([](MCRoomView& view) { view.LeftMissionPick(); });
}

auto MCRoomModel::SeatIndexOf(uint32_t playerId) const -> std::optional<size_t>
{
    const auto found = std::ranges::find(Seats, playerId, &MCSeat::Id);
    return found == Seats.end() ? std::nullopt : std::optional(static_cast<size_t>(found - Seats.begin()));
}

auto MCRoomModel::SortedPlayerIds(std::span<const uint32_t> ids) -> std::array<uint32_t, MaxPlayers>
{
    std::array<uint32_t, MaxPlayers> sorted;
    sorted.fill(NoPlayer);
    std::ranges::copy(ids.first(std::min(ids.size(), MaxPlayers)), sorted.begin());
    // Empty slots are the largest id, so they end up last.
    std::ranges::sort(sorted);
    return sorted;
}

auto MCRoomModel::Activate(bool refresh) -> void
{
    const bool isHost = MultiPlayer()->IsHost != 0;
    // Port-only (the personal build): a host opens its room alone and waits for players (the original's ready room
    // waited, and its GO took two); back from a match, or a guest, the room still takes two.
    const size_t fewest = isHost && !refresh ? 1 : 2;

    if (MultiPlayer()->SessionManager->GetPlayers(nullptr).size() < fewest)
    {
        Cancel();
        return;
    }

    PingUntil = MCPort::Milliseconds() + PingMs;
    Pinging = true;
    CanStart = false;
    // The view's tabs and buttons a new room's (SPEC-menus.md's stage 2: EXIT goes back one screen, as Escape does), and
    // its names hidden, each at its row.
    ToView([](MCRoomView& view) { view.Opening(); });

    for (MCSeat& seat : Seats)
    {
        seat.Shown = false;
    }

    CancelMission();
    Team2RP = 0;
    Team1RP = 0;
    Team1Players.fill(NoPlayer);
    Team2Players.fill(NoPlayer);
    NumUnassigned = 0;
    NumPlayers = 0;

    if (isHost && !refresh)
    {
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::SwitchScreen), 1, 0);
    }

    // Port-only: the room opens on the chat. Every build says what it plays by, and the host's match options go to the
    // guests (its last ones, kept for the session; at first its command line's) as far as every build plays them.
    MultiPlayer()->SendPlayerRules();

    if (isHost)
    {
        MultiPlayer()->SendMatchOptions();
    }

    Team1TechBase = InnerSphere;
    Team2TechBase = Clan;
    ToView([](MCRoomView& view) { view.ShowTechBases(); });

    // The players' ids in ascending order (the empty slots last).
    std::vector<uint32_t> sessionIds;
    const auto& players = MultiPlayer()->SessionManager->GetPlayers(nullptr);
    NumPlayers = static_cast<int32_t>(players.size());

    for (const auto& player : players)
    {
        sessionIds.push_back(player->Id);
    }

    const std::array<uint32_t, MaxPlayers> ids = SortedPlayerIds(sessionIds);

    if (GlobalLog()->PlayerLights == nullptr)
    {
        GlobalLog()->MakePlayerLights();
    }

    MCMPPlayerLights& lights = *GlobalLog()->PlayerLights;
    lights.SetNumPlayers(NumPlayers);
    size_t index = 0;
    // Port-only (the personal build): the host's guests whose games are up as they are shown the room (WatchRoom).
    ShownReady.clear();

    for (const auto& player : players)
    {
        if (player->HeardPing)
        {
            ShownReady.insert(player->Id);
        }
    }

    do
    {
        Seat(index, ids[index]);
        ShowSeat(index, true);
        AssignPlayer(ids[index], NoTeam, 0, true);
        lights.SetPlayerID(static_cast<int32_t>(index), ids[index]);
        lights.SetPlayerStatus(ids[index], refresh ? LightCheckingIn : LightIdle);
        ++index;
    } while (index < MaxPlayers && ids[index] != NoPlayer);

    if (!isHost || refresh)
    {
        ControlsOff();

        if (refresh)
        {
            // Back from a mission: wait for everyone to check in again.
            const std::array<std::string, 1> waiting = {LoadGameString(0xb8, 0xfe)};
            ToView([&waiting](MCRoomView& view) { view.ShowMissionText(CheckInFont, waiting); });
            CheckingIn = true;
            MCFIGuaranteedMessageHeader checkIn{};
            checkIn.Header = GuaranteedHeader(MCMPMessageType::SessionCheckIn);
            MultiPlayer()->SessionManager->SendMessageToGroup(0, LinkupBytes(checkIn));
            MultiPlayer()->PlayerSessionCheckIn[MultiPlayer()->SessionManager->MyPlayer()->PlayerNumber] = -1;
            SomeoneCheckedIn();
        }
    }
    else
    {
        ControlsOn();
    }

    MissionName.clear();
    MapName.clear();
    MissionTime.clear();
    // The chat, the ticker and the lights on the room's view.
    ToView([](MCRoomView& view) { view.Opened(); });

    // Port-only (the personal build): unique, as the room shown again opens the room again.
    if (_Ticker == nullptr)
    {
        _Ticker = MCGuiRef<MCRoomTicker>::Make();
    }

    GuiSystem().AddUniqueTimer(_Ticker.Pin(), TickTimer, TickMs, 0, 0, false);
    MultiPlayer()->ChatCallback = LogisticsChatCallback;
}

auto MCRoomModel::Tick() -> void
{
    if (MultiPlayer() == nullptr)
    {
        StopTick();
        return;
    }

    // Ping for the first three seconds so the latencies are known.
    if (Pinging)
    {
        if (MCPort::Milliseconds() < PingUntil)
        {
            if (MultiPlayer()->SessionManager->IsHost == 0)
            {
                MultiPlayer()->SessionManager->SendPing();
            }
        }
        else
        {
            Pinging = false;
        }
    }

    // Tell the others when the host changed a team's points: what it typed in the 1998 room's boxes, as they hold it.
    ToView([](MCRoomView& view) { view.TakeTypedPoints(); });
    Team1Points = ReadPoints(PointsText[0]);
    Team2Points = ReadPoints(PointsText[1]);

    if (Team1RP != Team1Points)
    {
        Team1RP = Team1Points;

        if (MultiPlayer()->IsHost != 0)
        {
            SendTwoLongs(GuaranteedHeader(MCMPMessageType::RPUpdate), Team1RP, 1);
        }
    }

    if (Team2RP != Team2Points)
    {
        Team2RP = Team2Points;

        if (MultiPlayer()->IsHost != 0)
        {
            SendTwoLongs(GuaranteedHeader(MCMPMessageType::RPUpdate), Team2RP, 2);
        }
    }

    WatchRoom();
}

auto MCRoomModel::StopTick() const -> void
{
    if (_Ticker != nullptr)
    {
        GuiSystem().RemoveTimer(*_Ticker, TickTimer);
    }
}

auto MCRoomModel::StopBoxTimers() -> void
{
    ToView([](MCRoomView& view) { view.StopBoxTimers(); });
}

auto MCRoomModel::ShowRoomAgain() -> void
{
    // The host's picks as they stand; the room then starts afresh, for everyone.
    const std::array<uint32_t, TeamSlots> team1 = Team1Players;
    const std::array<uint32_t, TeamSlots> team2 = Team2Players;
    const std::array<int8_t, 2> techBases = {Team1TechBase, Team2TechBase};
    const std::array<int32_t, 2> points = {Team1Points, Team2Points};
    // The host's own mission, or (Port-only, Redux HD) a room's taken over, whose host loaded it (TakeOverRoom).
    const std::string mission = LoadedMission;
    SDL_Log("Session screen: showing the room to %zu players again%s",
            MultiPlayer()->SessionManager->GetPlayers(nullptr).size(),
            mission.empty() ? "" : ", its mission asked of everyone again");
    Activate(false);

    // The mission first (every player asked for its file again, the newcomer too): its load sets its own points and,
    // when it locks them, its tech bases.
    if (!mission.empty())
    {
        LoadMission(mission);
    }

    // Each player still here back in its slot, told to everyone.
    const auto placeAgain = [this](int8_t team, const std::array<uint32_t, TeamSlots>& players)
    {
        for (size_t slot = 0; slot < TeamSlots; ++slot)
        {
            if (players[slot] != NoPlayer && SeatIndexOf(players[slot]).has_value())
            {
                AssignPlayer(players[slot], team, static_cast<int8_t>(slot), false);
            }
        }
    };

    placeAgain(Team1, team1);
    placeAgain(Team2, team2);
    // Port-only (Redux HD): a rematch's player back in its slot (network/MCRoomEnd.h).
    MCRoomEnd::PlaceReturning(*this);
    SetTeamsAgain(techBases, points);
}

auto MCRoomModel::SetTeamsAgain(const std::array<int8_t, 2>& techBases, const std::array<int32_t, 2>& points) -> void
{
    if (Locked)
    {
        return;
    }

    for (int8_t team = Team1; team <= Team2; ++team)
    {
        const int8_t techBase = techBases[static_cast<size_t>(team - 1)];
        SetTeamTechBase(team, techBase);
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::TechbaseChange), team, techBase);
    }

    // The tick sends them (each team's last sent is 0 now); the mission's load sends them again after its own.
    SetTeam1RP(points[0]);
    SetTeam2RP(points[1]);
}

auto MCRoomModel::Picks() const -> MCRoomPicks
{
    MCRoomPicks picks;
    picks.Mission = MissionFile.empty() ? std::string() : LoadedMission;
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    const auto names =
        [&manager](const std::array<uint32_t, TeamSlots>& players, std::array<std::string, TeamSlots>& into)
    {
        for (size_t slot = 0; slot < TeamSlots; ++slot)
        {
            const std::shared_ptr<MCFidpPlayer> player =
                players[slot] != NoPlayer ? manager.GetPlayer(players[slot]) : nullptr;
            into[slot] = player != nullptr ? player->Name : std::string();
        }
    };

    names(Team1Players, picks.Team1);
    names(Team2Players, picks.Team2);
    picks.TechBases = {Team1TechBase, Team2TechBase};
    picks.Points = {Team1Points, Team2Points};
    return picks;
}

auto MCRoomModel::TakePicks(const MCRoomPicks& picks) -> void
{
    if (!picks.Mission.empty())
    {
        LoadMission(picks.Mission);
    }

    PlaceByName(picks);
    SetTeamsAgain(picks.TechBases, picks.Points);
}

auto MCRoomModel::PlaceByName(const MCRoomPicks& picks) -> void
{
    MCSessionManager& manager = *MultiPlayer()->SessionManager;

    for (int8_t team = Team1; team <= Team2; ++team)
    {
        const std::array<std::string, TeamSlots>& names = team == Team1 ? picks.Team1 : picks.Team2;

        for (size_t slot = 0; slot < TeamSlots; ++slot)
        {
            const std::array<uint32_t, TeamSlots>& players = team == Team1 ? Team1Players : Team2Players;

            if (names[slot].empty() || players[slot] != NoPlayer)
            {
                continue;
            }

            // The player of that name in the room, on no team yet.
            const auto& inRoom = manager.GetPlayers(nullptr);
            const auto named = std::ranges::find_if(inRoom,
                                                    [&](const std::shared_ptr<MCFidpPlayer>& player)
                                                    {
                                                        return player != nullptr && player->Name == names[slot] &&
                                                               SeatIndexOf(player->Id).has_value() &&
                                                               FindSlot(Team1Players, player->Id) < 0 &&
                                                               FindSlot(Team2Players, player->Id) < 0;
                                                    });

            if (named != inRoom.end())
            {
                SDL_Log("Session screen: %s back in team %d's slot %zu", names[slot].c_str(), team, slot);
                AssignPlayer((*named)->Id, team, static_cast<int8_t>(slot), false);
            }
        }
    }
}

auto MCRoomModel::AllShown() const -> bool
{
    // A player leaving has lost its number (it is still listed while its leave is told).
    return std::ranges::all_of(
        MultiPlayer()->SessionManager->GetPlayers(nullptr), [this](const auto& player)
        { return player == nullptr || !player->HasPlayerNumber || SeatIndexOf(player->Id).has_value(); });
}

auto MCRoomModel::ShownWithGameUp(uint32_t playerId) const -> bool
{
    return ShownReady.contains(playerId);
}

auto MCRoomModel::WatchRoom() -> void
{
    MCMultiPlayer& game = *MultiPlayer();

    if (game.IsHost == 0 || !RoomCurrent())
    {
        return;
    }

    MCSessionManager& manager = *game.SessionManager;
    const auto& players = manager.GetPlayers(nullptr);

    // Port-only (Redux HD): a room rolled over to this game (TakeOverRoom). The host's choices were what the room played,
    // as far as every game in it plays them; this game's own (its command line's) are no longer the room's.
    if (std::exchange(TakingOver, false))
    {
        SDL_Log("Session screen: this game hosts the room now (its host has gone), %zu players in it", players.size());
        game.MatchOptions = game.Played;

        // The seats are filled afresh for the players left: one fewer than the room showed, so a seat past them would
        // keep its player's id (Activate sets only as many as there are).
        for (size_t seat = 0; seat < Seats.size(); ++seat)
        {
            Seat(seat, NoPlayer);
        }

        ShowRoomAgain();
        return;
    }

    // Port: a configured match (MPGameConfiguration) goes on as soon as every player it named has joined, straight to
    // the mech bay: no map screen or settings wait. A problem leaves the host here, with START.
    if (GameConfiguration.Active)
    {
        MPPollConfiguredMatch();

        if (players.size() >= GameConfiguration.Roster.size() &&
            std::ranges::all_of(GameConfiguration.Roster,
                                [&players](const MPGamePlayer& wanted)
                                {
                                    return std::ranges::any_of(players, [&wanted](const auto& player)
                                                               { return player->Name == wanted.Name; });
                                }))
        {
            MPBeginConfiguredMatch();
        }

        if (!RoomCurrent())
        {
            return;
        }
    }

    // Who joined, and whether each game is up. A player gone is forgotten.
    const uint32_t now = MCPort::Milliseconds();
    std::erase_if(SeenSince, [&manager](const auto& seen) { return manager.GetPlayer(seen.first) == nullptr; });
    bool showAgain = false;

    for (const auto& player : players)
    {
        if (player == nullptr || player->Id == manager.MyPlayerID || !player->HasPlayerNumber)
        {
            continue;
        }

        const uint32_t since = SeenSince.try_emplace(player->Id, now).first->second;

        if (!SeatIndexOf(player->Id).has_value())
        {
            showAgain = showAgain || player->HeardPing || now - since >= ShowAnywayMs;
        }
        else if (player->HeardPing && !ShownReady.contains(player->Id))
        {
            // Shown the room before its game was up: shown it again now that it is.
            showAgain = true;
        }
    }

    // START waits for a player who joined until it is shown.
    if (!AllShown())
    {
        CheckGoodToGo();
    }

    // Nothing under way in the room: a match, the wait for check-ins after one, a configured match's or a mission's
    // wait for the players' files, a dialog.
    const bool asking = AskingFiles();

    if (!showAgain || game.InLogistics || game.InMission || asking || MPConfiguredMatchWaiting() ||
        GlobalLog()->Message->IsShowing() || GlobalLog()->Question->IsShowing())
    {
        return;
    }

    ShowRoomAgain();
}

auto MCRoomModel::LayOutUnassigned(uint32_t skipId, size_t names, bool skipEmpty) -> void
{
    NumUnassigned = 0;

    for (size_t index = 0; index < names; ++index)
    {
        const uint32_t id = Seats[index].Id;

        if (skipEmpty ? id == NoPlayer : id == skipId)
        {
            continue;
        }

        if (FindSlot(Team1Players, id) >= 0 || FindSlot(Team2Players, id) >= 0)
        {
            continue;
        }

        ToView([index, row = NumUnassigned](MCRoomView& view) { view.NameToRow(index, row); });
        NumUnassigned++;
    }
}

auto MCRoomModel::AssignPlayer(uint32_t playerId, int8_t team, int8_t slot, bool remote) -> void
{
    const std::optional<size_t> seat = SeatIndexOf(playerId);
    const bool onTeam = team == Team1 || team == Team2;

    // Issue #25: the host's join-team message names the player and slot; a player the room doesn't list, or a slot
    // past the team's, is ignored (the original moved a null name and wrote past the team).
    if ((onTeam && (slot < 0 || static_cast<size_t>(slot) >= TeamSlots)) ||
        (!seat.has_value() && (onTeam || (team == NoTeam && !remote))))
    {
        return;
    }

    if (team == NoTeam)
    {
        if (remote)
        {
            // Filling the room: the player starts unassigned.
            NumUnassigned++;
            CheckGoodToGo();
            return;
        }

        int32_t index = FindSlot(Team1Players, playerId);
        uint32_t groupID = MultiPlayer()->InnerSphereGroupID;
        std::span<uint32_t> players = Team1Players;

        if (index < 0)
        {
            index = FindSlot(Team2Players, playerId);
            groupID = MultiPlayer()->ClanGroupID;
            players = Team2Players;
        }

        if (index >= 0)
        {
            ToView([&seat, row = NumUnassigned](MCRoomView& view) { view.NameToRow(*seat, row); });
            players[static_cast<size_t>(index)] = NoPlayer;
            MultiPlayer()->SessionManager->RemovePlayerFromGroup(groupID, playerId);
            NumUnassigned++;
        }
        else
        {
            LayOutUnassigned(NoPlayer, MaxPlayers, true);
        }

        GlobalLog()->PlayerLights->SetPlayerStatus(playerId, LightIdle);
    }
    else if (onTeam)
    {
        std::array<uint32_t, TeamSlots>& teamPlayers = team == Team1 ? Team1Players : Team2Players;
        const auto slotIndex = static_cast<size_t>(slot);

        if (teamPlayers[slotIndex] != NoPlayer)
        {
            AssignPlayer(teamPlayers[slotIndex], NoTeam, 0, false);
        }

        if (const int32_t index = FindSlot(Team1Players, playerId); index >= 0)
        {
            Team1Players[static_cast<size_t>(index)] = NoPlayer;
            MultiPlayer()->SessionManager->RemovePlayerFromGroup(MultiPlayer()->InnerSphereGroupID, playerId);
        }
        else if (const int32_t team2Index = FindSlot(Team2Players, playerId); team2Index >= 0)
        {
            Team2Players[static_cast<size_t>(team2Index)] = NoPlayer;
            MultiPlayer()->SessionManager->RemovePlayerFromGroup(MultiPlayer()->ClanGroupID, playerId);
        }
        else
        {
            LayOutUnassigned(playerId, static_cast<size_t>(NumPlayers), false);
        }

        teamPlayers[slotIndex] = playerId;
        const uint32_t groupID = team == Team1 ? MultiPlayer()->InnerSphereGroupID : MultiPlayer()->ClanGroupID;
        MultiPlayer()->SessionManager->AddPlayerToGroup(groupID, playerId);

        if (MultiPlayer()->SessionManager->MyPlayer()->Id == playerId)
        {
            MultiPlayer()->HomeTeam = team == Team1 ? 0 : 1;
            MultiPlayer()->HomeTeamGroupID = groupID;
            MultiPlayer()->EnemyTeamGroupID =
                team == Team1 ? MultiPlayer()->ClanGroupID : MultiPlayer()->InnerSphereGroupID;
        }

        ToView([&seat, team, slot](MCRoomView& view) { view.NameToTeam(*seat, team, slot); });
        GlobalLog()->PlayerLights->SetPlayerStatus(playerId, LightOnTeam);

        if (remote)
        {
            CheckGoodToGo();
            return;
        }
    }
    else if (remote)
    {
        CheckGoodToGo();
        return;
    }

    if (MultiPlayer()->IsHost != 0)
    {
        MCMPJoinTeamMessage message{};
        message.Header = GuaranteedHeader(MCMPMessageType::JoinTeam);
        message.PlayerID = playerId;
        message.Team = team;
        message.Slot = slot;
        MultiPlayer()->SessionManager->SendMessageToGroup(0, LinkupBytes(message));
    }

    CheckGoodToGo();
}

auto MCRoomModel::RemovePlayer(uint32_t playerId) -> void
{
    const std::optional<size_t> seat = playerId != NoPlayer ? SeatIndexOf(playerId) : std::nullopt;

    if (!seat.has_value())
    {
        return;
    }

    // Each game takes it off its own room (its own place-keeping times it out), so nothing is sent.
    if (const int32_t slot = FindSlot(Team1Players, playerId); slot >= 0)
    {
        Team1Players[static_cast<size_t>(slot)] = NoPlayer;
        MultiPlayer()->SessionManager->RemovePlayerFromGroup(MultiPlayer()->InnerSphereGroupID, playerId);
    }
    else if (const int32_t team2Slot = FindSlot(Team2Players, playerId); team2Slot >= 0)
    {
        Team2Players[static_cast<size_t>(team2Slot)] = NoPlayer;
        MultiPlayer()->SessionManager->RemovePlayerFromGroup(MultiPlayer()->ClanGroupID, playerId);
    }

    ShowSeat(*seat, false);
    Seat(*seat, NoPlayer);
    NumPlayers = std::max(NumPlayers - 1, 0);
    LayOutUnassigned(NoPlayer, MaxPlayers, true);
    ShownReady.erase(playerId);
    SeenSince.erase(playerId);
    CheckGoodToGo();

    // The mission's file asked of it: the answers go on without it (it may have been the last one waited for).
    if (const MCFileStatus asked = std::exchange(Seats.at(*seat).Files, MCFileStatus::NotAsked);
        asked == MCFileStatus::Waiting)
    {
        FilesAnswered();
    }
}

auto MCRoomModel::TakeOverRoom() -> void
{
    // On the room's next look (WatchRoom), once the host's leave is through.
    TakingOver = true;
}

auto MCRoomModel::SetPointsOf(int8_t team, int32_t resourcePoints) -> void
{
    (team == Team1 ? Team1Points : Team2Points) = resourcePoints;
    // As the 1998 room's box took the number: cut to its seven characters.
    PointsText[static_cast<size_t>(team - 1)] = std::format("{}", resourcePoints).substr(0, PointsBoxChars);
    ToView([team](MCRoomView& view) { view.ShowPoints(team); });
}

auto MCRoomModel::SetTeam1RP(int32_t resourcePoints) -> void
{
    SetPointsOf(Team1, resourcePoints);
}

auto MCRoomModel::SetTeam2RP(int32_t resourcePoints) -> void
{
    SetPointsOf(Team2, resourcePoints);
}

auto MCRoomModel::TakePoints(int32_t resourcePoints, int32_t team) -> void
{
    if (team == Team1)
    {
        SetTeam1RP(resourcePoints);
    }

    if (team == Team2)
    {
        SetTeam2RP(resourcePoints);
    }

    ToView([](MCRoomView& view) { view.DrawNow(); });
}

auto MCRoomModel::SetMap(std::string_view fileName) -> void
{
    // The view's picture of it (none: the map's box cleared, as the original wiped it in the background picture).
    MapFile = fileName;
    ToView([](MCRoomView& view) { view.ShowMap(); });
}

auto MCRoomModel::SetMissionName(std::string_view name) -> void
{
    MissionName = name;
}

auto MCRoomModel::SetMapName(std::string_view name) -> void
{
    MapName = name;
}

auto MCRoomModel::SomeoneCheckedIn() -> void
{
    bool allIn = true;

    for (size_t index = 0; index < Seats.size(); ++index)
    {
        const uint32_t id = Seats[index].Id;

        if (id == NoPlayer)
        {
            continue;
        }

        // Original behaviour: the check-in flags are read by name slot, not by player number.
        if (MultiPlayer()->PlayerSessionCheckIn[index] == 0)
        {
            allIn = false;
        }
        else
        {
            GlobalLog()->PlayerLights->SetPlayerStatus(id, LightIdle);
        }
    }

    if (allIn)
    {
        ToView([](MCRoomView& view) { view.ShowMissionText(DescriptionFont, {}); });
        CheckingIn = false;

        if (MultiPlayer()->IsHost != 0)
        {
            ControlsOn();
        }
    }
}

auto MCRoomModel::FileReport(uint32_t playerId, MCFileCheck check) -> void
{
    using Status = MCFileStatus;

    // Nothing to do unless an inquiry is out.
    if (std::ranges::all_of(Seats, [](const MCSeat& seat) { return seat.Files == Status::NotAsked; }))
    {
        return;
    }

    if (const MCOptionalRef<MCSeat> reporter = SeatOf(playerId); reporter.has_value())
    {
        constexpr auto statusOf = [](MCFileCheck said)
        {
            switch (said)
            {
                case MCFileCheck::Same:
                    return Status::Present;
                case MCFileCheck::Differs:
                    return Status::Differs;
                case MCFileCheck::Unchecked:
                    return Status::Unchecked;
                case MCFileCheck::Missing:
                    break;
            }

            return Status::Missing;
        };

        reporter->get().Files = statusOf(check);
    }

    FilesAnswered();
}

auto MCRoomModel::FilesAnswered() -> void
{
    using Status = MCFileStatus;

    // Port: the names of the players whose file is so.
    const auto named = [this](Status status)
    {
        return MCNamedPlayers(
            Seats, [status](const MCSeat& seat) { return seat.Files == status; },
            [](const MCSeat& seat) -> const std::string& { return seat.Name; });
    };

    bool allHave = true;

    for (const MCSeat& seat : Seats)
    {
        if (seat.Files == Status::Waiting)
        {
            return;
        }

        if (seat.Files == Status::Missing || seat.Files == Status::Differs)
        {
            allHave = false;
        }
    }

    if (allHave)
    {
        // Port: a player whose build can't check its game data (an older one) plays, as before; the host is told whose,
        // and its OK goes on to the load.
        if (const std::string unchecked = named(Status::Unchecked); !unchecked.empty())
        {
            ShowDialog(std::format(UncheckedText, unchecked), nullptr, "bh_okay.tga", "bg_okay.tga", MPLoadWorked);
            CheckGoodToGo();
            return;
        }

        // Everyone has it: the waiting dialog closes itself.
        MCMenuDialog& dialog = *GlobalLog()->Message;
        dialog.CloseIn(1000);
        dialog.Ok.Exec = nullptr;
        CheckGoodToGo();
        return;
    }

    // Name the players missing the file (the original's text, the names run together). Port: and, on a line of their
    // own, those whose game data differ from this game's.
    std::string text;
    const std::string differing = named(Status::Differs);

    for (MCSeat& seat : Seats)
    {
        if (seat.Files == Status::Missing)
        {
            text += text.empty() ? LoadGameString(0xab, 0xfe) + seat.Name : seat.Name;
        }

        seat.Files = Status::NotAsked;
    }

    if (!differing.empty())
    {
        text += (text.empty() ? "" : "\n") + std::format(DiffersText, differing);

        if (!MCReleaseData::Whole())
        {
            std::string lacking;

            for (const std::string& file : MCReleaseData::Lacking())
            {
                if (!lacking.empty())
                {
                    lacking += LackingSeparator;
                }

                lacking += file;
            }

            text += "\n" + std::format(LackingText, lacking);
        }
    }

    ShowDialog(text, MPCancel, "bh_okay.tga", "bg_okay.tga", nullptr);
}

auto MCRoomModel::LoadMission(std::string_view fileName) -> void
{
    MCPacketFile packetFile;
    MCFitIniFile iniFile;
    MCFile textFile;
    const std::string path =
        MultiPlayer()->IsHost == 0 ? GamePath(fileName, "", "") : GamePath(SavePath, fileName, ".mpk");

    // Reads st name (as the original's 0xfe-character buffer took it).
    auto readText = [&iniFile](std::string_view name, std::string& text) -> int32_t
    {
        MCFitResult<std::string> value = iniFile.Read<std::string>(name);

        if (!value.has_value())
        {
            return std::to_underlying(value.error());
        }

        if (value->size() >= 0xfe)
        {
            return BUFFER_TOO_SMALL;
        }

        text = std::move(*value);
        return 0;
    };

    std::string text;
    uint32_t value = 0;
    int32_t result = packetFile.Open(path);
    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open .MPK file ");

    if (result == 0)
    {
        result = packetFile.SeekPacket(0);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find packet 0 in .MPK file ");

    if (result == 0)
    {
        result = iniFile.Open(packetFile, packetFile.GetPacketSize());
        Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open packet 0 in .MPK file ");

        if (result == 0)
        {
            result = iniFile.SeekBlock("Multiplayer");
        }
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find Multiplayer block in file ");

    if (result == 0)
    {
        result = readText("LongMissionName", text);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't read mission name in Multiplayer file ");

    if (result == 0)
    {
        SetMissionName(text);
        result = readText("LongMapName", text);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't read map name in Multiplayer file ");

    if (result == 0)
    {
        SetMapName(text);
        result = readText("MapFileName", text);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't read map file name in Multiplayer file ");

    if (result == 0)
    {
        SetMap(text);

        // Port-only (SPEC-memory.md's "From the briefing": a match's map at the room's pick): its mission's art held from
        // now, behind the room, as a campaign's from its briefing; START's mech bay keeps it, another map lets go of it.
        if (const MCFitResult<std::string> mission = iniFile.Read<std::string>("MissionName"); mission.has_value())
        {
            StartMissionHold(*mission);
        }

        // OB-195 (fixed): the original set the room's time limit only from a file that has one, so a map with none
        // (Arena, Capture Base, Many Bases, Swamp Rat and others) went on showing the last map's. The room's alone: the
        // battle's own is its mission's (MCScenario::LoadObjectives' TimeLeft).
        MissionTime.clear();

        if (iniFile.ReadIdULong("MissionTime", value) == 0)
        {
            MissionTime = std::format("{}:{:02}", value / 60, value % 60);
        }

        result = iniFile.SeekBlock("ResourcePoints");
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find ResourcePoints block in MPK file ");

    if (result == 0)
    {
        const bool team1Missing = iniFile.ReadIdULong("Team1Points", value) != 0;

        if (!team1Missing)
        {
            SetTeam1RP(static_cast<int32_t>(value));
        }

        const bool team2Missing = iniFile.ReadIdULong("Team2Points", value) != 0;

        if (!team2Missing)
        {
            SetTeam2RP(static_cast<int32_t>(value));
        }

        if (team1Missing || team2Missing)
        {
            // An older file: one figure for both teams.
            if (iniFile.ReadIdULong("numPoints", value) != 0)
            {
                value = 0;
            }

            if (team1Missing)
            {
                SetTeam1RP(static_cast<int32_t>(value));
            }

            if (team2Missing)
            {
                SetTeam2RP(static_cast<int32_t>(value));
            }
        }
    }

    if (MultiPlayer()->IsHost != 0)
    {
        LockControls(iniFile.SeekBlock("Lock") == 0);
    }

    iniFile.Close();

    // Packet 1: the mission description.
    result = packetFile.SeekPacket(1);
    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find packet 1 in .MPK file ");

    if (result == 0)
    {
        result = textFile.Open(packetFile, packetFile.GetPacketSize());
        Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open packet 1 in .MPK file ");

        if (result == 0)
        {
            Description.clear();

            while (!textFile.Eof())
            {
                Description.push_back(textFile.ReadLine(0xfe));
            }

            ToView([this](MCRoomView& view) { view.ShowMissionText(DescriptionFont, Description); });
            Assert(textFile.Eof(), 0, "Error reading MP mission description");

            if (MultiPlayer()->IsHost != 0)
            {
                // Ask everyone whether they have the file, and wait for the answers.
                MissionFile = path;
                LoadedMission = fileName;
                MultiPlayer()->SendFileInquiry(MissionFile.data());
                // The server has it already.
                const uint32_t serverId = MultiPlayer()->SessionManager->ServerID;

                for (MCSeat& seat : Seats)
                {
                    if (seat.Id != NoPlayer)
                    {
                        seat.Files = MCFileStatus::Waiting;
                    }

                    if (seat.Id == serverId)
                    {
                        seat.Files = MCFileStatus::Present;
                    }
                }

                ShowDialog(LoadGameString(0xac, 0xfe), MPCancel, "bh_cancl.tga", "bg_cancl.tga", MPLoadWorked);
                // Port-only (the personal build): with no ready room the host may be alone in the room, and then nobody
                // answers: the check is done at once.
                FilesAnswered();
            }
            else
            {
                // Port-only (Redux HD): a guest keeps the mission's name as its host loads it (the path's file name,
                // without the extension), for its game to load it as the room's host should the room roll over to it.
                const size_t folderEnd = fileName.find_last_of("/\\");
                const std::string_view name =
                    folderEnd == std::string_view::npos ? fileName : fileName.substr(folderEnd + 1);
                LoadedMission = std::string(name.substr(0, name.rfind('.')));
            }

            return;
        }
    }

    // The file couldn't be read.
    SetMissionName({});
    SetMapName({});
    SetMap({});
    MissionTime.clear();
    SetTeam1RP(0);
    SetTeam2RP(0);
    Description.clear();

    // Port-only (Redux HD): a guest holds no mission it couldn't read (its room shows none; a room rolled over to it
    // loads none), not the one its host named before.
    if (MultiPlayer()->IsHost == 0)
    {
        LoadedMission.clear();
    }

    ToView([](MCRoomView& view) { view.ShowMissionText(DescriptionFont, {}); });
    ShowDialog(MCFormatPrintf(LoadGameString(0xad, 0xfe).c_str(), std::string(fileName).c_str()), MPCancel,
               "bh_okay.tga", "bg_okay.tga", nullptr);
}

auto MCRoomModel::CancelMission() -> void
{
    SetMissionName({});
    SetMapName({});
    SetMap({});
    MissionFile.clear();
    LoadedMission.clear();
    Locked = false;
    MissionTime.clear();
    SetTeam1RP(0);
    SetTeam2RP(0);
    Description.clear();
    ToView([](MCRoomView& view) { view.ShowMissionText(DescriptionFont, {}); });

    for (MCSeat& seat : Seats)
    {
        seat.Files = MCFileStatus::NotAsked;
    }
}

auto MCRoomModel::PublishMatchState() -> void
{
    MPMatchState& match = MultiPlayer()->Match;
    match.Team1Players = Team1Players;
    match.Team2Players = Team2Players;
    match.Team1TechBase = Team1TechBase;
    match.Team2TechBase = Team2TechBase;
    // As the mech bay read the typed points before: the boxes' text.
    ToView([](MCRoomView& view) { view.TakeTypedPoints(); });
    match.Team1Points = std::atoi(PointsText[0].c_str());
    match.Team2Points = std::atoi(PointsText[1].c_str());
    // Port-only (Redux HD): the room the match is played from, which its end goes back to (network/MCRoomEnd.h); an
    // internet game's room on MCO shown in game there (network/MCMcoSeats.h).
    MCRoomEnd::MatchStarts();
    MCMcoSeats::MatchStarts();
}

auto MCRoomModel::CheckGoodToGo() -> void
{
    // Port-only (the personal build): a guest away updating (network/MCLobbyUpdate.h) isn't counted: START needs a player
    // there on each team, and a guest away unpicked doesn't hold it.
    const auto there = [](uint32_t id) { return id != NoPlayer && !MCLobbyUpdate::Away(id); };
    const auto filled = [&](const auto& players) { return std::ranges::any_of(players, there); };
    const auto awayUnpicked =
        static_cast<int32_t>(std::ranges::count_if(Seats,
                                                   [&](const MCSeat& seat)
                                                   {
                                                       return MCLobbyUpdate::Away(seat.Id) &&
                                                              FindSlot(Team1Players, seat.Id) < 0 &&
                                                              FindSlot(Team2Players, seat.Id) < 0;
                                                   }));
    const std::optional<std::vector<uint32_t>> homeTeam = MultiPlayer()->PlayersOnHomeTeam();
    const std::optional<std::vector<uint32_t>> enemyTeam = MultiPlayer()->PlayersOnEnemyTeam();

    const bool goodToGo = NumUnassigned == awayUnpicked && !MissionFile.empty() && homeTeam.has_value() &&
                          filled(*homeTeam) && enemyTeam.has_value() && filled(*enemyTeam) && filled(Team1Players) &&
                          filled(Team2Players) && AllShown();

    // Port-only: while a guest away updating has its place kept, its line is in the update's line's place, and START
    // asks before it starts without it.
    StartWaits = MCLobbyUpdate::StartWaitsText();
    CanStart = goodToGo;
    ToView([](MCRoomView& view) { view.ShowStart(); });
}

auto MCRoomModel::SetTeamTechBase(int8_t team, int8_t techBase) -> void
{
    if (techBase != InnerSphere && techBase != Clan)
    {
        return;
    }

    if (team == Team1)
    {
        Team1TechBase = techBase;
    }
    else if (team == Team2)
    {
        Team2TechBase = techBase;
    }
    else
    {
        return;
    }

    // The chosen toggle on, the other off.
    ToView([](MCRoomView& view) { view.ShowTechBases(); });
}

auto MCRoomModel::PickTechBase(int8_t team, int8_t techBase) -> void
{
    SetTeamTechBase(team, techBase);

    if (MultiPlayer()->IsHost != 0)
    {
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::TechbaseChange), team, techBase);
    }
}

auto MCRoomModel::Begin() -> void
{
    StartMission();
}

auto MCRoomModel::ControlsOn() -> void
{
    HostControls = true;
    ForcesLive = true;
    ToView([](MCRoomView& view) { view.ShowControls(); });
}

auto MCRoomModel::ControlsOff() -> void
{
    HostControls = false;
    ForcesLive = false;
    ToView([](MCRoomView& view) { view.ShowControls(); });
}

auto MCRoomModel::LockControls(bool lock) -> void
{
    ForcesLive = !lock;
    ToView([](MCRoomView& view) { view.ShowControls(); });
    Locked = lock;

    if (lock)
    {
        // A locked mission: fixed points and tech bases (team 1 Inner Sphere, team 2 Clan).
        SetTeamTechBase(Team1, InnerSphere);
        SetTeamTechBase(Team2, Clan);
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::TechbaseChange), Team1, InnerSphere);
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::TechbaseChange), Team2, Clan);
    }
}
