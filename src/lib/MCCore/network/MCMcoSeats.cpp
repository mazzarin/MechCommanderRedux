#include "stdafx.h"
#include "network/MCMcoSeats.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCMcoRooms.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRemove.h"
#include "platform/MCDirectPlay.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>A host's MCO room for its internet game, as far as it has gone.</summary>
    struct MCHostSide
    {
        /// <summary>The internet game it's for: a new one starts again.</summary>
        const MCInternetGame* For = nullptr;
        /// <summary>HostBoth was asked, and when last.</summary>
        bool Asked = false;
        Clock::time_point AskedAt;
        /// <summary>The room as hosted, for the guests: its name, password and most players.</summary>
        std::string Name;
        std::string Password;
        int32_t Most = 0;
        /// <summary>The room's id once MCO took it, and its seats as last heard (the host's first).</summary>
        std::string RoomId;
        std::vector<MCMcoApi::MCRoomPlayer> RoomSeats;
        uint64_t Revision = 0;
        /// <summary>Each guest told the room: the room's id, and when.</summary>
        std::map<uint32_t, std::pair<std::string, Clock::time_point>> Told;
        /// <summary>Each guest's seat as it said.</summary>
        std::map<uint32_t, MCMcoSeats::MCSeatSaid> Seats;
        /// <summary>The description and most players MCO was last asked for.</summary>
        std::optional<std::pair<std::string, int32_t>> Described;
        /// <summary>MCO's session is on (LET IN, or the match started there): no one waits until it closes.</summary>
        bool Live = false;
        /// <summary>MCO's waiting players, and what the guests were last told of them.</summary>
        std::vector<MCMcoSeats::MCWaiting> Waiting;
        std::map<uint32_t, std::vector<MCMcoSeats::MCWaiting>> ToldWaiting;
        /// <summary>Each removed player's seat kicked, by its MCO id: the room's revision then (once a poll).</summary>
        std::map<std::string, uint64_t> Kicked;
    };

    /// <summary>A guest's seat in its host's MCO room.</summary>
    struct MCGuestSide
    {
        /// <summary>The room it was told to sit in.</summary>
        std::string RoomId;
        /// <summary>The seat it last told the host.</summary>
        std::optional<MCMcoSeats::MCSeatSaid> Told;
        /// <summary>MCO's players waiting, as the host last said.</summary>
        std::vector<MCMcoSeats::MCWaiting> Waiting;
        uint64_t Revision = 0;
    };

    /// <summary>The tests' MCO (MCMcoSeats::SetWaitingForTests): its waiting players, and its LET IN.</summary>
    struct MCTestsMco
    {
        std::vector<MCMcoSeats::MCWaiting> Waiting;
        /// <summary>Whether LET IN fails from its next press on, and whether its last press failed.</summary>
        bool Fails = false;
        bool Failed = false;
        int32_t LetIns = 0;
    };

    MCHostSide Host;
    MCGuestSide Guest;
    /// <summary>The players the host removed, kept out of its MCO room a while (past one internet game: its host's).</summary>
    std::vector<MCMcoSeats::MCKeptOut> KeptOut;
    bool HostsSwitch = false;
    std::optional<MCTestsMco> TestsMco;

    /// <summary>Whether the game was started by a launcher (MCO's, or another's): its room is where it put it.</summary>
    bool LaunchersGame()
    {
        return LobbyLaunchArgs.Active && !LobbyLaunchArgs.FromMenus;
    }

    /// <summary><paramref name="text"/> in <paramref name="field"/>, cut to leave its zero.</summary>
    template <size_t N> void Put(std::array<char, N>& field, std::string_view text)
    {
        field.fill('\0');
        std::ranges::copy(text.substr(0, N - 1), field.begin());
    }

    /// <summary>The text in <paramref name="field"/>; none without its zero.</summary>
    template <size_t N> std::optional<std::string> Got(const std::array<char, N>& field)
    {
        const auto end = std::ranges::find(field, '\0');
        return end != field.end() ? std::optional(std::string(field.begin(), end)) : std::nullopt;
    }

    bool InSession(MCSessionManager& manager, uint32_t id)
    {
        return manager.GetPlayer(id) != nullptr;
    }

    /// <summary>The host's guests in the session (not itself).</summary>
    std::vector<uint32_t> GuestsOf(MCSessionManager& manager)
    {
        std::vector<uint32_t> guests;

        for (const std::shared_ptr<MCFidpPlayer>& player : manager.GetPlayers(nullptr))
        {
            if (player != nullptr && player->Id != manager.MyPlayerID)
            {
                guests.push_back(player->Id);
            }
        }

        return guests;
    }

    /// <summary>The internet game hosted on MCO too, once its code is known and the session is up.</summary>
    void AskHost(MCSessionManager& manager, const MCFidpSession& session)
    {
        if (!HostsSwitch || LaunchersGame())
        {
            return;
        }

        const uint16_t port = manager.DirectPlay != nullptr ? manager.DirectPlay->HostPort() : uint16_t{0};

        if (port == 0)
        {
            return;
        }

        MCInternetGame& internet = *manager.Internet;
        Host.Asked = true;
        Host.AskedAt = Clock::now();
        // Named as our service's list shows the game, so INTERNET leaves out this MCO room only for this game.
        Host.Name = MCServiceGameName(session.Name);
        Host.Password = internet.Public() ? std::string() : internet.Code();
        Host.Most = MCMcoApi::MatchCapacity(static_cast<int32_t>(session.SessionDesc.dwMaxPlayers));
        SDL_Log("MCO seats: hosting game %s on MechCommander Online too", internet.Code().c_str());
        MCMcoRooms::Now().HostBoth({.Name = Host.Name,
                                    .MaxPlayers = Host.Most,
                                    .Password = Host.Password,
                                    .GamePort = port,
                                    .Carry = internet.Carrier()});
    }

    /// <summary>The MCO room as the model last heard it.</summary>
    void HearRoom()
    {
        MCMcoRooms& mco = MCMcoRooms::Now();

        if (const uint64_t revision = mco.Revision(); revision != Host.Revision)
        {
            Host.Revision = revision;
            const MCMcoSeen seen = mco.Seen();
            const bool ours = seen.Room.has_value() && seen.Room->Both && seen.Room->Hosting;
            std::string roomId = ours ? seen.Room->RoomId : std::string();

            // A new room is described afresh.
            if (roomId != Host.RoomId)
            {
                Host.Described.reset();
            }

            Host.RoomId = std::move(roomId);
            Host.RoomSeats = ours ? seen.Room->Players : std::vector<MCMcoApi::MCRoomPlayer>();
            Host.Live = ours && seen.Match != MCMcoMatchStage::None;
        }
    }

    /// <summary>Whether <paramref name="rules"/> are a Redux HD build's, which a room's word may reach.</summary>
    bool ReduxHd(uint32_t rules)
    {
        return (rules & MCMatchRule::Rematch) != 0;
    }

    /// <summary>Each Redux HD guest told the room (an earlier build ignores it), once a room.</summary>
    void TellGuests(MCMultiPlayer& game, MCSessionManager& manager)
    {
        for (const uint32_t guest : GuestsOf(manager))
        {
            const auto rules = game.PlayerRules.find(guest);
            const auto told = Host.Told.find(guest);

            if (rules == game.PlayerRules.end() || !ReduxHd(rules->second) ||
                (told != Host.Told.end() && told->second.first == Host.RoomId))
            {
                continue;
            }

            MCMPMcoRoomMessage message = MCMcoSeats::RoomMessage(
                {.RoomId = Host.RoomId, .Name = Host.Name, .Password = Host.Password, .MaxPlayers = Host.Most});
            manager.SendPlainMessage(guest, LinkupBytes(message));
            Host.Told[guest] = {Host.RoomId, Clock::now()};
            Host.Seats.erase(guest);
        }
    }

    /// <summary>The session's players' names.</summary>
    std::set<std::string> NamesOf(MCSessionManager& manager)
    {
        std::set<std::string> names;

        for (const std::shared_ptr<MCFidpPlayer>& player : manager.GetPlayers(nullptr))
        {
            if (player != nullptr)
            {
                names.insert(player->Name);
            }
        }

        return names;
    }

    /// <summary>Whether <paramref name="guest"/> is one of MCO's players let in: its seat is MCO's own.</summary>
    bool FromMco(MCSessionManager& manager, uint32_t guest)
    {
        const std::shared_ptr<MCFidpPlayer> player = manager.GetPlayer(guest);
        return player != nullptr && std::ranges::any_of(Host.RoomSeats, [&](const MCMcoApi::MCRoomPlayer& seat)
                                                        { return !seat.Host && seat.Username == player->Name; });
    }

    /// <summary>Each guest told the room told who waits in it, as that changes.</summary>
    void TellWaiting(MCSessionManager& manager)
    {
        std::erase_if(Host.ToldWaiting, [&](const auto& told) { return !InSession(manager, told.first); });

        for (const auto& [guest, room] : Host.Told)
        {
            if (room.first != Host.RoomId)
            {
                continue;
            }

            if (const auto told = Host.ToldWaiting.find(guest);
                told != Host.ToldWaiting.end() && told->second == Host.Waiting)
            {
                continue;
            }

            MCMPMcoWaitingMessage message = MCMcoSeats::WaitingMessage(Host.Waiting);
            manager.SendPlainMessage(guest, LinkupBytes(message));
            Host.ToldWaiting[guest] = Host.Waiting;
        }
    }

    /// <summary>The guests with no seat in the room (MCMcoSeats::HasNoSeat), MCO's own players let in aside.</summary>
    size_t Unseated(MCMultiPlayer& game, MCSessionManager& manager)
    {
        const Clock::time_point now = Clock::now();
        return static_cast<size_t>(std::ranges::count_if(
            GuestsOf(manager),
            [&](uint32_t guest)
            {
                if (FromMco(manager, guest))
                {
                    return false;
                }

                MCMcoSeats::MCGuestSeat known;

                if (const auto rules = game.PlayerRules.find(guest); rules != game.PlayerRules.end())
                {
                    known.Rules = rules->second;
                }

                if (const auto told = Host.Told.find(guest); told != Host.Told.end())
                {
                    known.Told = told->second.second;
                }

                if (const auto seat = Host.Seats.find(guest); seat != Host.Seats.end())
                {
                    known.Said = seat->second.Seat;
                }

                return MCMcoSeats::HasNoSeat(known, now);
            }));
    }

    void TendHost(MCMultiPlayer& game, MCSessionManager& manager)
    {
        MCInternetGame& internet = *manager.Internet;
        const std::shared_ptr<MCFidpSession> session = manager.CurrentSession();

        if (!internet.Hosting() || internet.Code().empty() || session == nullptr)
        {
            return;
        }

        if (Host.For != &internet)
        {
            Host = {.For = &internet};
        }

        HearRoom();

        // No room (not yet asked, or gone: off for a battle, refused, MCO not answering): asked for again in the room.
        if (Host.RoomId.empty())
        {
            if (!game.InMission && (!Host.Asked || Clock::now() - Host.AskedAt >= MCMcoSeats::RehostEvery))
            {
                AskHost(manager, *session);
            }

            return;
        }

        // A guest gone takes its seat with it.
        std::erase_if(Host.Told, [&](const auto& told) { return !InSession(manager, told.first); });
        std::erase_if(Host.Seats, [&](const auto& seat) { return !InSession(manager, seat.first); });
        TellGuests(game, manager);

        std::set<std::string> seated;

        for (const auto& [guest, seat] : Host.Seats)
        {
            if (seat.Seat == MCMcoSeats::MCSeat::Taken)
            {
                seated.insert(seat.ClientId);
            }
        }

        // A removed player back in the room is taken out again (once a poll), and never waits.
        const Clock::time_point now = Clock::now();
        std::erase_if(KeptOut,
                      [&](const MCMcoSeats::MCKeptOut& kept) { return now - kept.At >= MCRemove::KeptOutFor; });
        const std::vector<std::string> kicks = MCMcoSeats::KeptOutSeats(Host.RoomSeats, KeptOut);

        for (const std::string& clientId : kicks)
        {
            if (const auto kicked = Host.Kicked.find(clientId);
                kicked == Host.Kicked.end() || kicked->second != Host.Revision)
            {
                Host.Kicked[clientId] = Host.Revision;
                MCMcoRooms::Now().Kick(clientId);
            }
        }

        std::vector<MCMcoApi::MCRoomPlayer> seats;
        std::ranges::copy_if(Host.RoomSeats, std::back_inserter(seats), [&](const MCMcoApi::MCRoomPlayer& seat)
                             { return std::ranges::find(kicks, seat.ClientId) == kicks.end(); });

        // After LET IN no one waits: an MCO player whose game closed waits for the next match, its place not held.
        Host.Waiting =
            Host.Live ? std::vector<MCMcoSeats::MCWaiting>() : MCMcoSeats::WaitingIn(seats, seated, NamesOf(manager));
        manager.DirectPlay->SetReserved(static_cast<uint32_t>(Host.Waiting.size()));
        TellWaiting(manager);

        // The mission picked is the room's description; MCO takes no more players than the game has room for.
        const std::string mission =
            GlobalLog() != nullptr && GlobalLog()->Room.Made() ? GlobalLog()->Room.MissionName : std::string();
        const int32_t most = MCMcoSeats::RoomMost(static_cast<int32_t>(session->SessionDesc.dwMaxPlayers),
                                                  Unseated(game, manager), Host.RoomSeats.size());

        if (const std::pair<std::string, int32_t> described{mission, most}; Host.Described != described)
        {
            Host.Described = described;
            MCMcoRooms::Now().Describe(mission, most);
        }
    }

    /// <summary>The host told this guest's seat, when it isn't what it last told.</summary>
    void TellSeat(MCSessionManager& manager, const MCMcoSeats::MCSeatSaid& said)
    {
        if (Guest.Told != said)
        {
            MCMPMcoSeatMessage message = MCMcoSeats::SeatMessage(said);
            manager.SendPlainMessage(manager.ServerID, LinkupBytes(message));
            Guest.Told = said;
        }
    }

    void TendGuest(MCSessionManager& manager)
    {
        if (Guest.RoomId.empty())
        {
            return;
        }

        MCMcoRooms& mco = MCMcoRooms::Now();
        const uint64_t revision = mco.Revision();

        if (revision == Guest.Revision)
        {
            return;
        }

        Guest.Revision = revision;
        const MCMcoSeen seen = mco.Seen();

        // Told once MCO has answered for this room, and again if the seat goes.
        if (seen.SeatRoom != Guest.RoomId)
        {
            return;
        }

        TellSeat(manager, seen.Seated
                              ? MCMcoSeats::MCSeatSaid{.Seat = MCMcoSeats::MCSeat::Taken, .ClientId = seen.ClientId}
                              : MCMcoSeats::MCSeatSaid{});
    }

    /// <summary>The game whose internet game is tended, with MCO in it; none otherwise.</summary>
    std::shared_ptr<MCMultiPlayer> InternetGame()
    {
        std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        return game != nullptr && game->SessionManager != nullptr && game->SessionManager->Internet != nullptr &&
                       MCMcoRooms::Available()
                   ? game
                   : nullptr;
    }
}

void MCMcoSeats::Frame()
{
    const std::shared_ptr<MCMultiPlayer> game = InternetGame();

    if (game == nullptr)
    {
        return;
    }

    if (MCSessionManager& manager = *game->SessionManager; manager.IsHost)
    {
        TendHost(*game, manager);
    }
    else
    {
        TendGuest(manager);
    }
}

void MCMcoSeats::FillRoom(MCServiceRoom& room)
{
    if (Host.RoomId.empty())
    {
        return;
    }

    room.Mco = Host.RoomId;
    room.Players = RoomPlayers(std::move(room.Players), Host.Waiting, room.Max);
}

std::vector<std::string> MCMcoSeats::RoomPlayers(std::vector<std::string> players,
                                                 const std::vector<MCWaiting>& waiting, int32_t most)
{
    // Never more than the room's most: the service refuses such a room, ending the game. The door holds the waiting
    // players' places, so only a join meanwhile overfills it.
    for (const MCWaiting& one : waiting)
    {
        if (std::cmp_greater_equal(players.size(), most))
        {
            break;
        }

        players.push_back(one.Name);
    }

    return players;
}

void MCMcoSeats::HostSaidRoom(uint32_t fromId, std::span<const uint8_t> bytes)
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    if (game == nullptr || game->SessionManager == nullptr || game->SessionManager->Internet == nullptr ||
        game->SessionManager->IsHost || fromId != game->SessionManager->ServerID)
    {
        return;
    }

    MCSessionManager& manager = *game->SessionManager;
    const std::optional<MCRoomSaid> said = RoomSaidIn(bytes);

    if (!said.has_value() || !MCMcoRooms::Available())
    {
        if (!said.has_value())
        {
            SDL_Log("MCO seats: the host's word of its MCO room isn't one; no seat taken");
        }

        TellSeat(manager, {});
        return;
    }

    if (said->RoomId == Guest.RoomId)
    {
        return;
    }

    MCMcoRooms& mco = MCMcoRooms::Now();

    if (!Guest.RoomId.empty())
    {
        mco.Unsit();
    }

    Guest = {.RoomId = said->RoomId};
    TellSeat(manager, {.Seat = MCSeat::Asked});
    mco.Sit(said->RoomId, said->Name, said->MaxPlayers, said->Password);
}

void MCMcoSeats::GuestSaidSeat(uint32_t fromId, std::span<const uint8_t> bytes)
{
    const std::shared_ptr<MCMultiPlayer> game = InternetGame();
    const std::optional<MCSeatSaid> seat = SeatSaidIn(bytes);

    if (game == nullptr || !game->SessionManager->IsHost || Host.RoomId.empty())
    {
        return;
    }

    if (!seat.has_value())
    {
        SDL_Log("MCO seats: player %u's word of its seat isn't one; it's taken as none", fromId);
    }

    Host.Seats[fromId] = seat.value_or(MCSeatSaid{});
}

void MCMcoSeats::Ended()
{
    if (MCMcoRooms::Available())
    {
        if (Host.Asked)
        {
            MCMcoRooms::Now().LeaveBoth();
        }

        if (!Guest.RoomId.empty())
        {
            MCMcoRooms::Now().Unsit();
        }
    }

    Host = {};
    Guest = {};
}

bool MCMcoSeats::KeepOut(std::optional<uint32_t> playerId, std::string_view name)
{
    // The tests' MCO: its waiting player goes.
    if (TestsMco.has_value())
    {
        return std::erase_if(TestsMco->Waiting, [&](const MCWaiting& waiting) { return waiting.Name == name; }) != 0;
    }

    // Its seat: a guest's as it said, else the one named (not the host's).
    std::string clientId;

    if (const auto said = playerId.has_value() ? Host.Seats.find(*playerId) : Host.Seats.end();
        said != Host.Seats.end() && said->second.Seat == MCSeat::Taken)
    {
        clientId = said->second.ClientId;
    }
    else if (const auto seat = std::ranges::find_if(Host.RoomSeats, [&](const MCMcoApi::MCRoomPlayer& one)
                                                    { return !one.Host && one.Username == name; });
             seat != Host.RoomSeats.end())
    {
        clientId = seat->ClientId;
    }

    // Kept by name too: a player whose seat wasn't known is taken out should it sit down.
    KeptOut.push_back({.ClientId = clientId, .Name = std::string(name), .At = Clock::now()});
    std::erase_if(Host.Waiting, [&](const MCWaiting& waiting) { return waiting.Name == name; });

    if (clientId.empty() || Host.RoomId.empty() || !MCMcoRooms::Available())
    {
        return false;
    }

    SDL_Log("MCO seats: %.*s removed from the internet game's MCO room", static_cast<int>(name.size()), name.data());
    Host.Kicked[clientId] = Host.Revision;
    MCMcoRooms::Now().Kick(clientId);
    return true;
}

std::vector<std::string> MCMcoSeats::KeptOutSeats(const std::vector<MCMcoApi::MCRoomPlayer>& seats,
                                                  const std::vector<MCKeptOut>& kept)
{
    std::vector<std::string> ids;

    for (const MCMcoApi::MCRoomPlayer& seat : seats)
    {
        if (!seat.Host &&
            std::ranges::any_of(
                kept, [&](const MCKeptOut& one)
                { return (!one.ClientId.empty() && one.ClientId == seat.ClientId) || one.Name == seat.Username; }))
        {
            ids.push_back(seat.ClientId);
        }
    }

    return ids;
}

std::vector<MCMcoSeats::MCWaiting> MCMcoSeats::Waiting()
{
    if (TestsMco.has_value())
    {
        return TestsMco->Waiting;
    }

    const std::shared_ptr<MCMultiPlayer> game = InternetGame();

    if (game == nullptr)
    {
        return {};
    }

    return game->SessionManager->IsHost ? Host.Waiting : Guest.Waiting;
}

void MCMcoSeats::HostSaidWaiting(uint32_t fromId, std::span<const uint8_t> bytes)
{
    const std::shared_ptr<MCMultiPlayer> game = InternetGame();

    if (game == nullptr || game->SessionManager->IsHost || fromId != game->SessionManager->ServerID)
    {
        return;
    }

    if (std::optional<std::vector<MCWaiting>> waiting = WaitingSaidIn(bytes))
    {
        Guest.Waiting = std::move(*waiting);
    }
    else
    {
        SDL_Log("MCO seats: the host's word of who waits in its MCO room isn't one; it's left as it was");
    }
}

void MCMcoSeats::LetIn()
{
    if (TestsMco.has_value())
    {
        TestsMco->LetIns++;
        TestsMco->Failed = TestsMco->Fails;
        return;
    }

    if (const std::shared_ptr<MCMultiPlayer> game = InternetGame();
        game != nullptr && game->SessionManager->IsHost && !Host.RoomId.empty())
    {
        MCMcoRooms::Now().StartBoth(false);
    }
}

bool MCMcoSeats::LetInFailed()
{
    if (TestsMco.has_value())
    {
        return TestsMco->Failed;
    }

    // MCO's refusal stays until its next ask, and LET IN's ask clears it at once (MCMcoRooms::StartBoth).
    const std::shared_ptr<MCMultiPlayer> game = InternetGame();
    return game != nullptr && game->SessionManager->IsHost &&
           MCMcoRooms::Now().Seen().Refusal == MCMcoRooms::LetInFailedText;
}

void MCMcoSeats::MatchStarts()
{
    if (const std::shared_ptr<MCMultiPlayer> game = InternetGame();
        game != nullptr && game->SessionManager->IsHost && !Host.RoomId.empty())
    {
        MCMcoRooms::Now().StartBoth(true);
    }
}

void MCMcoSeats::MatchOver()
{
    if (const std::shared_ptr<MCMultiPlayer> game = InternetGame();
        game != nullptr && game->SessionManager->IsHost && Host.Asked)
    {
        MCMcoRooms::Now().BothMatchOver();
    }
}

void MCMcoSeats::SetHostsOn(bool on)
{
    HostsSwitch = on;
}

bool MCMcoSeats::HostsOn()
{
    return HostsSwitch;
}

MCMPMcoRoomMessage MCMcoSeats::RoomMessage(const MCRoomSaid& said)
{
    MCMPMcoRoomMessage message{};
    message.Header = PlainHeader(MCMPMessageType::McoRoom);
    Put(message.RoomId, said.RoomId);
    Put(message.Name, said.Name);
    Put(message.Password, said.Password);
    message.MaxPlayers = static_cast<int8_t>(std::clamp(said.MaxPlayers, MCMcoApi::MatchFewest, MCMcoApi::RoomMost));
    return message;
}

std::optional<MCMcoSeats::MCRoomSaid> MCMcoSeats::RoomSaidIn(std::span<const uint8_t> bytes)
{
    if (bytes.size() < sizeof(MCMPMcoRoomMessage))
    {
        return std::nullopt;
    }

    const auto message = ReadLinkupMessage<MCMPMcoRoomMessage>(bytes);
    std::optional<std::string> roomId = Got(message.RoomId);
    std::optional<std::string> name = Got(message.Name);
    std::optional<std::string> password = Got(message.Password);

    if (!roomId || !MCMcoApi::IsHex(*roomId, MCMcoApi::ClientIdDigits) || !name || name->empty() || !password ||
        message.MaxPlayers < MCMcoApi::MatchFewest || message.MaxPlayers > MCMcoApi::RoomMost)
    {
        return std::nullopt;
    }

    return MCRoomSaid{.RoomId = std::move(*roomId),
                      .Name = std::move(*name),
                      .Password = std::move(*password),
                      .MaxPlayers = message.MaxPlayers};
}

MCMPMcoSeatMessage MCMcoSeats::SeatMessage(const MCSeatSaid& said)
{
    MCMPMcoSeatMessage message{};
    message.Header = PlainHeader(MCMPMessageType::McoSeat);
    message.Seat = std::to_underlying(said.Seat);
    Put(message.ClientId, said.Seat == MCSeat::Taken ? std::string_view(said.ClientId) : std::string_view());
    return message;
}

std::optional<MCMcoSeats::MCSeatSaid> MCMcoSeats::SeatSaidIn(std::span<const uint8_t> bytes)
{
    if (bytes.size() < sizeof(MCMPMcoSeatMessage))
    {
        return std::nullopt;
    }

    const auto message = ReadLinkupMessage<MCMPMcoSeatMessage>(bytes);
    std::optional<std::string> clientId = Got(message.ClientId);
    const auto seat = static_cast<MCSeat>(message.Seat);
    const bool taken = seat == MCSeat::Taken;

    if (message.Seat > std::to_underlying(MCSeat::Taken) || !clientId ||
        taken != MCMcoApi::IsHex(*clientId, MCMcoApi::ClientIdDigits) || (!taken && !clientId->empty()))
    {
        return std::nullopt;
    }

    return MCSeatSaid{.Seat = seat, .ClientId = std::move(*clientId)};
}

bool MCMcoSeats::HasNoSeat(const MCGuestSeat& guest, Clock::time_point now)
{
    if (!guest.Rules.has_value())
    {
        return false;
    }

    if (!ReduxHd(*guest.Rules))
    {
        return true;
    }

    if (guest.Said.has_value())
    {
        return *guest.Said == MCSeat::None;
    }

    return guest.Told.has_value() && now - *guest.Told >= AnswerWait;
}

int32_t MCMcoSeats::RoomMost(int32_t gameMost, size_t unseated, size_t seatsTaken)
{
    const int32_t open = gameMost - static_cast<int32_t>(std::min<size_t>(unseated, MCMcoApi::RoomMost));
    return MCMcoApi::MatchCapacity(
        std::max(open, static_cast<int32_t>(std::min<size_t>(seatsTaken, MCMcoApi::RoomMost))));
}

std::vector<MCMcoSeats::MCWaiting> MCMcoSeats::WaitingIn(const std::vector<MCMcoApi::MCRoomPlayer>& seats,
                                                         const std::set<std::string>& seated,
                                                         const std::set<std::string>& names)
{
    std::vector<MCWaiting> waiting;

    for (const MCMcoApi::MCRoomPlayer& seat : seats)
    {
        if (!seat.Host && !seated.contains(seat.ClientId) && !names.contains(seat.Username))
        {
            waiting.push_back({.Name = seat.Username, .Ready = seat.Ready});
        }
    }

    return waiting;
}

MCMPMcoWaitingMessage MCMcoSeats::WaitingMessage(const std::vector<MCWaiting>& waiting)
{
    MCMPMcoWaitingMessage message{};
    message.Header = PlainHeader(MCMPMessageType::McoWaiting);
    message.Count = static_cast<uint8_t>(std::min(waiting.size(), message.Players.size()));

    for (size_t i = 0; i < message.Count; ++i)
    {
        Put(message.Players[i].Name, waiting[i].Name);
        message.Players[i].Ready = waiting[i].Ready ? 1 : 0;
    }

    return message;
}

std::optional<std::vector<MCMcoSeats::MCWaiting>> MCMcoSeats::WaitingSaidIn(std::span<const uint8_t> bytes)
{
    if (bytes.size() < sizeof(MCMPMcoWaitingMessage))
    {
        return std::nullopt;
    }

    const auto message = ReadLinkupMessage<MCMPMcoWaitingMessage>(bytes);

    if (message.Count > message.Players.size())
    {
        return std::nullopt;
    }

    std::vector<MCWaiting> waiting;

    for (size_t i = 0; i < message.Count; ++i)
    {
        std::optional<std::string> name = Got(message.Players[i].Name);

        if (!name || name->empty() || message.Players[i].Ready > 1)
        {
            return std::nullopt;
        }

        waiting.push_back({.Name = std::move(*name), .Ready = message.Players[i].Ready == 1});
    }

    return waiting;
}

void MCMcoSeats::SetWaitingForTests(std::optional<std::vector<MCWaiting>> waiting)
{
    if (!waiting.has_value())
    {
        TestsMco.reset();
        return;
    }

    if (!TestsMco.has_value())
    {
        TestsMco.emplace();
    }

    TestsMco->Waiting = std::move(*waiting);
}

void MCMcoSeats::FailLetInForTests(bool fail)
{
    if (!TestsMco.has_value())
    {
        TestsMco.emplace();
    }

    TestsMco->Fails = fail;
}

int32_t MCMcoSeats::LetInsForTests()
{
    return TestsMco.has_value() ? TestsMco->LetIns : 0;
}
