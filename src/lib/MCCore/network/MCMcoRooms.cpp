#include "stdafx.h"
#include "network/MCMcoRooms.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <random>

namespace
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    /// <summary>MCO's codes that end the player's sign-in (the launcher signs the player out on them).</summary>
    constexpr std::array<std::string_view, 5> SignOutCodes{
        "peer_auth_failed", "peer_token_missing", "account_session_invalid", "profile_deleted", "moderation_deleted"};
    constexpr std::string_view WrongSignInCode = "invalid_credentials";
    constexpr std::array<std::string_view, 2> AccountLimitCodes{"device_account_limit", "ip_account_limit"};
    /// <summary>HTTP's "too many requests", and the first of its server errors.</summary>
    constexpr int TooManyStatus = 429;
    constexpr int ServerErrorStatus = 500;
    /// <summary>The room's polls that may fail in a row (MCO unreachable, busy) before the room is given up.</summary>
    constexpr int PollFailuresMost = 10;
    /// <summary>How often a guest tries a match the relay or its host wasn't ready for, and how long between.</summary>
    constexpr int MatchTries = 5;
    constexpr auto MatchRetryEvery = std::chrono::seconds(3);
    /// <summary>What a relay or MCO says of a match whose session has gone (the launcher reports it).</summary>
    constexpr std::string_view SessionGoneText = "no longer available";
    /// <summary>With nothing to poll, the thread waits for a request this long at most.</summary>
    constexpr auto IdleWait = std::chrono::seconds(60);

    /// <summary>The kept sign-in's fields, as its text names them.</summary>
    constexpr std::string_view KeptClientId = "client_id";
    constexpr std::string_view KeptPeerToken = "peer_token";
    constexpr std::string_view KeptAccountToken = "account_token";
    constexpr std::string_view KeptUsername = "username";

    /// <summary>MCO's own chat lines, in the screens' words, by their room_event (as MCO's launcher says them).</summary>
    struct MCEventSaid
    {
        std::string_view Event;
        std::string_view Format;
    };

    constexpr std::array<MCEventSaid, 14> EventsSaid{{
        {"joined", "{} joined the room."},
        {"left", "{} left the room."},
        {"removed", "{} was removed from the room."},
        {"disconnected", "{} lost their connection."},
        {"lease_expired", "{} lost their connection."},
        {"reconnected", "{} reconnected."},
        {"host_transferred", "{} is now the host."},
        {"host_closed", "{} closed the room."},
        {"host_connection_lost", "The room closed after {} lost their connection."},
        {"name_changed", "{} renamed the room."},
        {"description_changed", "{} changed the room's description."},
        {"locked", "{} locked the room."},
        {"unlocked", "{} unlocked the room."},
        {"password_changed", "{} changed the room's password."},
    }};
    /// <summary>MCO's own lines of a match's start (event_kind), and what its line says of the second.</summary>
    constexpr std::string_view LaunchRequestedKind = "LaunchRequested";
    constexpr std::string_view LaunchRequestedFormat = "{} started the game.";
    constexpr std::string_view GameStartedKind = "GameStarted";
    constexpr std::string_view GameStartedText = "The game started.";

    /// <summary>A message that carries no text, by its kind (type), as its sender's line says it.</summary>
    struct MCKindSaid
    {
        std::string_view Kind;
        std::string_view Said;
    };

    constexpr std::array<MCKindSaid, 4> KindsSaid{{
        {"gif", "sent a GIF."},
        {"image", "sent a picture."},
        {"voice", "sent a voice message."},
        {"audio", "sent a voice message."},
    }};

    std::unique_ptr<MCMcoRooms> Current;

    /// <summary><paramref name="text"/> without its leading and trailing spaces, at most <paramref name="most"/> bytes (a whole character last).</summary>
    std::string Trimmed(std::string_view text, size_t most)
    {
        const size_t first = text.find_first_not_of(" \t\r\n");

        if (first == std::string_view::npos)
        {
            return {};
        }

        text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);

        if (text.size() <= most)
        {
            return std::string(text);
        }

        // Cut before a UTF-8 character's continuation bytes, never inside it.
        size_t end = most;

        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
        {
            end--;
        }

        return std::string(text.substr(0, end));
    }

    /// <summary>Whether <paramref name="text"/> says a match's session has gone (any case).</summary>
    bool SaysSessionGone(std::string_view text)
    {
        std::string lower(text);
        std::ranges::transform(lower, lower.begin(),
                               [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        return lower.contains(SessionGoneText);
    }

    /// <summary>Overwrites a password or token before it's let go.</summary>
    void Wipe(std::string& text)
    {
        std::ranges::fill(text, '\0');
        text.clear();
    }

    /// <summary>The host among <paramref name="players"/>, else none.</summary>
    std::string HostOf(const std::vector<MCMcoApi::MCRoomPlayer>& players)
    {
        const auto host =
            std::ranges::find_if(players, [](const MCMcoApi::MCRoomPlayer& player) { return player.Host; });
        return host == players.end() ? std::string() : host->Username;
    }
}

std::string MCMcoKeptText(const MCMcoKept& kept)
{
    return Json{{KeptClientId, kept.ClientId},
                {KeptPeerToken, kept.PeerToken},
                {KeptAccountToken, kept.AccountToken},
                {KeptUsername, kept.Username}}
        .dump();
}

std::optional<MCMcoKept> MCMcoKeptFrom(std::string_view text)
{
    const Json json = Json::parse(text, nullptr, false);

    if (!json.is_object())
    {
        return std::nullopt;
    }

    const auto field = [&](std::string_view name) -> std::string
    {
        const auto value = json.find(name);
        return value != json.end() && value->is_string() ? value->get<std::string>() : std::string();
    };

    MCMcoKept kept{.ClientId = field(KeptClientId),
                   .PeerToken = field(KeptPeerToken),
                   .AccountToken = field(KeptAccountToken),
                   .Username = field(KeptUsername)};

    // Only what MCO could have given: a client id, a peer token for a guest, an account token for an account.
    const bool account = MCMcoApi::IsHex(kept.AccountToken, MCMcoApi::TokenDigits);
    const bool guest = kept.AccountToken.empty() && MCMcoApi::IsHex(kept.PeerToken, MCMcoApi::TokenDigits);

    if (!MCMcoApi::IsHex(kept.ClientId, MCMcoApi::ClientIdDigits) || !(account || guest) ||
        (!kept.PeerToken.empty() && !MCMcoApi::IsHex(kept.PeerToken, MCMcoApi::TokenDigits)))
    {
        return std::nullopt;
    }

    return kept;
}

void MCMcoRooms::Setup(MCMcoSetup setup)
{
    Current.reset();

    if (setup.Get)
    {
        Current = std::make_unique<MCMcoRooms>(std::move(setup));
    }
}

void MCMcoRooms::Release()
{
    Current.reset();
}

bool MCMcoRooms::Available()
{
    return Current != nullptr;
}

MCMcoRooms& MCMcoRooms::Now()
{
    return *Current;
}

bool MCMcoRooms::InMatch()
{
    if (!Available())
    {
        return false;
    }

    // Port-only (Redux HD): an internet game's room on MCO plays its match on our links, the room's end its own.
    const std::scoped_lock lock(Current->_Lock);
    const MCMcoMatchStage stage = Current->_Seen.Match;
    const bool both = Current->_Seen.Room.has_value() && Current->_Seen.Room->Both;
    return !both && (stage == MCMcoMatchStage::HostGame || stage == MCMcoMatchStage::JoinGame ||
                     stage == MCMcoMatchStage::Playing);
}

MCMcoRooms::MCMcoRooms(MCMcoSetup setup)
    : _Setup(std::move(setup))
    , _Transfer(_Setup.Get())
    , _Service(std::make_unique<MCMcoApi::MCService>(*_Transfer, _Setup.Client))
{
    if (!_Setup.GamePort)
    {
        _Setup.GamePort = [] { return MCMcoBridge::FreeGamePort(); };
    }

    _Thread = std::thread([this] { Run(); });
}

MCMcoRooms::~MCMcoRooms()
{
    {
        const std::scoped_lock lock(_Lock);
        _Stopping = true;
    }

    _Cancel = true;
    _Wake.notify_all();

    if (_Thread.joinable())
    {
        _Thread.join();
    }

    // The bridges' threads end with them; MCO drops a player whose polls stop.
    _Guest.reset();
    _Hosted.reset();
}

void MCMcoRooms::Post(MCJob job)
{
    {
        const std::scoped_lock lock(_Lock);
        _Jobs.push_back(std::move(job));
    }

    _Wake.notify_all();
}

void MCMcoRooms::Run()
{
    std::unique_lock lock(_Lock);

    while (!_Stopping)
    {
        if (!_Jobs.empty())
        {
            MCJob job = std::move(_Jobs.front());
            _Jobs.pop_front();
            lock.unlock();
            job();
            lock.lock();
            continue;
        }

        lock.unlock();
        const Clock::time_point next = Tend();
        lock.lock();
        _Wake.wait_until(lock, next, [this] { return _Stopping || !_Jobs.empty(); });
    }
}

std::chrono::milliseconds MCMcoRooms::PollGap()
{
    static thread_local std::minstd_rand random{std::random_device{}()};
    const auto least = _Setup.PollLeast.count();
    const auto most = std::max(_Setup.PollMost.count(), least);
    return std::chrono::milliseconds(
        std::uniform_int_distribution<std::chrono::milliseconds::rep>(least, most)(random));
}

Clock::time_point MCMcoRooms::Tend()
{
    const bool signedIn = !_Peer.PeerToken.empty();

    if (!signedIn || (!_Seat && !_LobbyWanted))
    {
        return Clock::now() + IdleWait;
    }

    if (Clock::now() >= _NextPoll)
    {
        if (_Seat)
        {
            PollRoom();
        }
        else
        {
            PollLobby();
        }

        _NextPoll = Clock::now() + PollGap();
    }

    return _NextPoll;
}

// The account.

void MCMcoRooms::Resume()
{
    Post([this] { DoResume(); });
}

void MCMcoRooms::DoResume()
{
    if (std::exchange(_Resumed, true) || !_Setup.Keys || !_Peer.PeerToken.empty())
    {
        return;
    }

    const std::optional<MCMcoKept> kept = _Setup.Keys->Load();

    if (!kept)
    {
        return;
    }

    _AccountKept = !kept->AccountToken.empty();

    if (kept->AccountToken.empty())
    {
        // A guest's profile goes on as it was (a lapsed peer token is renewed at its next request).
        _Peer = {.ClientId = kept->ClientId, .PeerToken = kept->PeerToken, .Username = kept->Username};
        Show(
            [&](MCMcoSeen& seen)
            {
                seen.Account = MCMcoAccount::Guest;
                seen.Username = kept->Username;
            });
        return;
    }

    Show(
        [](MCMcoSeen& seen)
        {
            seen.Busy = true;
            seen.Doing = std::string(ResumingText);
            seen.Refusal.clear();
        });
    const auto signIn = _Service->Resume(kept->AccountToken, kept->ClientId);

    if (!signIn)
    {
        // A sign-in MCO no longer takes is forgotten; MCO not answering keeps it for the next start.
        if (!signIn.error().Unreachable && signIn.error().Status < ServerErrorStatus)
        {
            _Setup.Keys->Erase();
        }

        Refused(signIn.error());
        return;
    }

    Activated(*signIn, false);
}

void MCMcoRooms::SignIn(std::string email, std::string password)
{
    Post(
        [this, email = std::move(email), password = std::move(password)]() mutable
        {
            Show(
                [](MCMcoSeen& seen)
                {
                    seen.Busy = true;
                    seen.Doing = std::string(SigningInText);
                    seen.Refusal.clear();
                });
            const auto signIn = _Service->Login(email, password);
            Wipe(password);

            if (!signIn)
            {
                Refused(signIn.error());
                return;
            }

            Activated(*signIn, false);
        });
}

void MCMcoRooms::Activated(const MCMcoApi::MCSignIn& signIn, bool takeOver)
{
    auto peerToken = _Service->Activate(signIn.AccountToken, takeOver);

    if (!peerToken && peerToken.error().Code == MCMcoApi::ActiveDeviceCode && !takeOver)
    {
        _Waiting = signIn;
        Show(
            [&](MCMcoSeen& seen)
            {
                seen.Busy = false;
                seen.Account = MCMcoAccount::OtherComputer;
                seen.Username = signIn.Username;
            });
        return;
    }

    if (!peerToken)
    {
        Refused(peerToken.error());
        return;
    }

    SignedIn(signIn, std::move(*peerToken));
}

void MCMcoRooms::SignedIn(const MCMcoApi::MCSignIn& signIn, std::string peerToken)
{
    // A guest's profile made an account keeps its place in the lobby: registered again as the account.
    _InLobby = false;
    _Waiting.reset();
    _Peer = {.ClientId = signIn.ClientId,
             .PeerToken = std::move(peerToken),
             .AccountToken = signIn.AccountToken,
             .Username = signIn.Username};

    if (_Setup.Keys && !_Setup.Keys->Save({.ClientId = _Peer.ClientId,
                                           .PeerToken = _Peer.PeerToken,
                                           .AccountToken = _Peer.AccountToken,
                                           .Username = _Peer.Username}))
    {
        SDL_Log(
            "MechCommander Online: the sign-in couldn't be kept in the system's store; it lasts until the game quits");
    }

    _NextPoll = Clock::now();
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Busy = false;
            seen.Account = MCMcoAccount::SignedIn;
            seen.Username = signIn.Username;
        });
}

void MCMcoRooms::TakeOver(bool here)
{
    Post(
        [this, here]
        {
            if (!_Waiting)
            {
                return;
            }

            if (!here)
            {
                _Waiting.reset();
                Show(
                    [](MCMcoSeen& seen)
                    {
                        seen.Account = MCMcoAccount::SignedOut;
                        seen.Username.clear();
                    });
                return;
            }

            Show(
                [](MCMcoSeen& seen)
                {
                    seen.Busy = true;
                    seen.Doing = std::string(SigningInText);
                    seen.Refusal.clear();
                });
            const MCMcoApi::MCSignIn signIn = *_Waiting;
            Activated(signIn, true);
        });
}

void MCMcoRooms::CreateAccount(std::string name, std::string email, std::string password)
{
    Post(
        [this, name = std::move(name), email = std::move(email), password = std::move(password)]() mutable
        {
            const auto username = MCMcoApi::PlayerName(name);

            if (!username)
            {
                Wipe(password);
                Refused(username.error());
                return;
            }

            Show(
                [](MCMcoSeen& seen)
                {
                    seen.Busy = true;
                    seen.Doing = std::string(CreatingText);
                    seen.Refusal.clear();
                });
            const auto can = _Service->CanCreate();

            if (!can || !*can)
            {
                Wipe(password);
                can ? Refused(std::string(ThreeAccountsText)) : Refused(can.error());
                return;
            }

            // A guest's profile becomes the account (its client id and peer token sent with it).
            const MCMcoApi::MCPeer guest = _Peer.AccountToken.empty() ? _Peer : MCMcoApi::MCPeer{};
            const auto signIn = _Service->Create(guest, *username, email, password);
            Wipe(password);

            if (!signIn)
            {
                Refused(signIn.error());
                return;
            }

            Activated(*signIn, false);
        });
}

void MCMcoRooms::PlayAsGuest(std::string name)
{
    Post([this, name = std::move(name)] { DoGuest(name); });
}

void MCMcoRooms::DoGuest(const std::string& name)
{
    const auto username = MCMcoApi::PlayerName(name);

    if (!username)
    {
        Refused(username.error());
        return;
    }

    Show(
        [](MCMcoSeen& seen)
        {
            seen.Busy = true;
            seen.Doing = std::string(SigningInText);
            seen.Refusal.clear();
        });
    MCMcoApi::MCPeer guest{.ClientId = MCMcoApi::NewClientId(), .Username = *username};

    // The guest's first registration (in the lobby) gives its peer token.
    if (const auto registered = _Service->Register(guest); !registered)
    {
        Refused(registered.error());
        return;
    }

    _Peer = std::move(guest);
    _InLobby = true;

    KeepGuest();

    _NextPoll = Clock::now();
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Busy = false;
            seen.Account = MCMcoAccount::Guest;
            seen.Username = _Peer.Username;
        });
}

void MCMcoRooms::SecureProfile(std::string email, std::string password)
{
    Post(
        [this, email = std::move(email), password = std::move(password)]() mutable
        {
            if (_Peer.PeerToken.empty() || !_Peer.AccountToken.empty())
            {
                Wipe(password);
                return;
            }

            Show(
                [](MCMcoSeen& seen)
                {
                    seen.Busy = true;
                    seen.Doing = std::string(CreatingText);
                    seen.Refusal.clear();
                });
            const auto signIn = _Service->Secure(_Peer, email, password);
            Wipe(password);

            if (!signIn)
            {
                Refused(signIn.error());
                return;
            }

            Activated(*signIn, false);
        });
}

void MCMcoRooms::SignOut()
{
    Post([this] { DoSignOut(); });
}

void MCMcoRooms::DoSignOut()
{
    if (_Seat)
    {
        DoLeave();
    }

    if (_InLobby)
    {
        if (const auto out = _Service->Unregister(_Peer); !out)
        {
            SDL_Log("MechCommander Online: leaving the lobby at the sign-out: %s",
                    MCMcoApi::Describe(out.error()).c_str());
        }
    }

    if (!_Peer.AccountToken.empty())
    {
        if (const auto out = _Service->Logout(_Peer.AccountToken); !out)
        {
            SDL_Log("MechCommander Online: signing out: %s", MCMcoApi::Describe(out.error()).c_str());
        }
    }

    Forget();
}

void MCMcoRooms::Forget()
{
    if (_Setup.Keys)
    {
        _Setup.Keys->Erase();
    }

    Wipe(_Peer.PeerToken);
    Wipe(_Peer.AccountToken);
    _Peer = {};
    _InLobby = false;
    _Seat.reset();
    _Stock.clear();
    Show(
        [](MCMcoSeen& seen)
        {
            const uint64_t revision = seen.Revision;
            const std::string refusal = seen.Refusal;
            seen = {};
            seen.Revision = revision;
            seen.Refusal = refusal;
        });
}

bool MCMcoRooms::SignsOut(const MCMcoApi::MCError& error)
{
    if (std::ranges::find(SignOutCodes, error.Code) == SignOutCodes.end())
    {
        return false;
    }

    SDL_Log("MechCommander Online signed the player out: %s", MCMcoApi::Describe(error).c_str());
    Forget();
    Show([&](MCMcoSeen& seen) { seen.Refusal = RefusalText(error); });
    return true;
}

// The lobby.

void MCMcoRooms::OpenLobby(std::string guestName)
{
    Post(
        [this, guestName = std::move(guestName)]
        {
            _LobbyWanted = true;
            _NextPoll = Clock::now();
            DoResume();

            if (guestName.empty())
            {
                return;
            }

            // INTERNET signs no one in: a player with no sign-in kept plays as a guest under its name.
            if (_Peer.PeerToken.empty() && !_AccountKept)
            {
                DoGuest(GuestName(guestName));
                return;
            }

            Rename(guestName);
        });
}

void MCMcoRooms::KeepGuest()
{
    if (_Setup.Keys &&
        !_Setup.Keys->Save({.ClientId = _Peer.ClientId, .PeerToken = _Peer.PeerToken, .Username = _Peer.Username}))
    {
        SDL_Log("MechCommander Online: the guest's profile (%s) couldn't be kept in the system's store; it lasts until "
                "the game quits",
                _Peer.Username.c_str());
    }
}

std::string MCMcoRooms::GuestName(const std::string& name)
{
    const auto username = MCMcoApi::PlayerName(name);
    return username ? *username : std::string(GuestNameNone);
}

void MCMcoRooms::Rename(const std::string& name)
{
    const std::string username = GuestName(name);

    bool guest = false;

    {
        const std::scoped_lock lock(_Lock);
        guest = _Seen.Account == MCMcoAccount::Guest;
    }

    if (!guest || username == _Peer.Username)
    {
        return;
    }

    // MCO takes a guest's name as each request names it (register_peer, join_room).
    SDL_Log("MechCommander Online: the guest's name is now %s", username.c_str());
    _Peer.Username = username;

    KeepGuest();

    Show([&](MCMcoSeen& seen) { seen.Username = _Peer.Username; });
}

void MCMcoRooms::CloseLobby()
{
    Post(
        [this]
        {
            _LobbyWanted = false;

            // A room the player is in (a match under way) stays: only the lobby's list is let go.
            if (_Seat || !_InLobby)
            {
                return;
            }

            if (const auto out = _Service->Unregister(_Peer); !out)
            {
                SDL_Log("MechCommander Online: leaving the lobby: %s", MCMcoApi::Describe(out.error()).c_str());
            }

            _InLobby = false;
            Show(
                [](MCMcoSeen& seen)
                {
                    seen.Listed = false;
                    seen.Rooms.clear();
                });
        });
}

bool MCMcoRooms::Registered()
{
    if (!_InLobby)
    {
        if (const auto registered = _Service->Register(_Peer); !registered)
        {
            if (!SignsOut(registered.error()))
            {
                Show([&](MCMcoSeen& seen) { seen.LobbyTrouble = RefusalText(registered.error()); });
            }

            return false;
        }

        _InLobby = true;
    }

    if (_Stock.empty())
    {
        const auto stock = _Service->StockBuild(_Peer);

        if (stock && *stock)
        {
            _Stock = **stock;
        }
        else if (!stock)
        {
            SDL_Log("MechCommander Online: the build its Redux rooms play: %s",
                    MCMcoApi::Describe(stock.error()).c_str());
        }
    }

    return true;
}

void MCMcoRooms::PollLobby()
{
    if (!Registered())
    {
        return;
    }

    const auto lobby = _Service->Lobby(_Peer);

    if (!lobby)
    {
        if (!SignsOut(lobby.error()))
        {
            Show([&](MCMcoSeen& seen) { seen.LobbyTrouble = RefusalText(lobby.error()); });
        }

        return;
    }

    std::vector<MCMcoApi::MCRoom> rooms;

    for (const MCMcoApi::MCRoom& room : lobby->Rooms)
    {
        if (room.Engine == MCMcoApi::ReduxEngine && (_Stock.empty() || room.EngineBuild == _Stock))
        {
            rooms.push_back(room);
        }
    }

    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Listed = true;
            seen.Rooms = std::move(rooms);
            seen.Online = lobby->Players;
            seen.LobbyTrouble.clear();
        });
}

// The room.

bool MCMcoRooms::Prepared(bool quiet)
{
    if (_Content.empty() && _Setup.Content)
    {
        const auto content = _Setup.Content();

        if (!content)
        {
            SDL_Log("MechCommander Online: the game data's fingerprint: %s", content.error().c_str());

            if (!quiet)
            {
                Refused(std::string(NoMatchText));
            }

            return false;
        }

        _Content = *content;
    }

    return Registered();
}

void MCMcoRooms::Join(std::string roomId, std::string password, std::string guestName)
{
    // Busy from the ask (each way the join ends says so), the last refusal gone: a screen waits on it at once.
    Show(
        [&](MCMcoSeen& seen)
        {
            const auto listed =
                std::ranges::find_if(seen.Rooms, [&](const MCMcoApi::MCRoom& each) { return each.RoomId == roomId; });
            const std::string shown =
                listed != seen.Rooms.end() ? MCMcoApi::Printable(listed->Name) : std::string(SomeRoom);
            seen.Busy = true;
            seen.Doing = std::vformat(JoiningFormat, std::make_format_args(shown));
            seen.Refusal.clear();
            seen.Left.clear();
        });
    Post(
        [this, roomId = std::move(roomId), password = std::move(password), guestName = std::move(guestName)]() mutable
        {
            if (!guestName.empty())
            {
                Rename(guestName);
            }

            DoJoin(roomId, std::move(password));
        });
}

void MCMcoRooms::DoJoin(const std::string& roomId, std::string password)
{
    std::optional<MCMcoApi::MCRoom> room;

    {
        const std::scoped_lock lock(_Lock);
        const auto listed =
            std::ranges::find_if(_Seen.Rooms, [&](const MCMcoApi::MCRoom& each) { return each.RoomId == roomId; });

        if (listed != _Seen.Rooms.end())
        {
            room = *listed;
        }
    }

    if (_Seat)
    {
        Wipe(password);
        Show([](MCMcoSeen& seen) { seen.Busy = false; });
        return;
    }

    if (!room)
    {
        Wipe(password);
        Refused(std::string(GoneText));
        return;
    }

    // The game's data read, the player in the lobby (Prepared says why not when its data can't be read).
    if (!Prepared())
    {
        Wipe(password);

        if (!_Content.empty())
        {
            Refused(std::string(UnreachableText));
        }

        return;
    }

    const std::string shown = MCMcoApi::Printable(room->Name);

    // As MCO's launcher holds a room to the player's data (a room naming none is joined, its match held to it).
    if (!room->EngineContent.empty() && !MCMcoApi::SameId(room->EngineContent, _Content))
    {
        Wipe(password);
        Refused(std::string(OtherDataText));
        return;
    }

    if (room->InGame || room->AdmissionClosed)
    {
        Wipe(password);
        Refused(std::vformat(StartedFormat, std::make_format_args(shown)));
        return;
    }

    if (room->MaxPlayers > 0 && room->Players >= room->MaxPlayers)
    {
        Wipe(password);
        Refused(std::vformat(FullFormat, std::make_format_args(shown)));
        return;
    }

    auto seat = _Service->JoinRoom(_Peer, *room, password, _Content, true);
    Wipe(password);

    if (!seat)
    {
        if (!SignsOut(seat.error()))
        {
            Refused(seat.error());
        }

        return;
    }

    _Seat = std::move(*seat);
    // The room's chat from its start, as MCO's launcher's room reads it: the first poll brings its latest lines.
    _Seat->LastMessageId = 0;
    _InLobby = false;
    _NextPoll = Clock::now();
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Busy = false;
            seen.Room = MCMcoRoomSeen{
                .RoomId = room->RoomId, .Name = room->Name, .MaxPlayers = room->MaxPlayers, .Host = room->Host};
        });
}

void MCMcoRooms::Host(std::string name, int32_t maxPlayers, std::string password)
{
    Post([this, name = std::move(name), maxPlayers, password = std::move(password)]() mutable
         { DoHost(name, maxPlayers, std::move(password)); });
}

void MCMcoRooms::DoHost(const std::string& name, int32_t maxPlayers, std::string password)
{
    if (_Seat || !Prepared())
    {
        Wipe(password);
        return;
    }

    if (_Stock.empty())
    {
        Wipe(password);
        Refused(std::string(NoBuildText));
        return;
    }

    const int32_t players = MCMcoApi::MatchCapacity(maxPlayers);
    Show(
        [](MCMcoSeen& seen)
        {
            seen.Busy = true;
            seen.Doing = std::string(HostingText);
            seen.Refusal.clear();
            seen.Left.clear();
        });
    auto seat = _Service->HostRoom(_Peer, name, players, password, _Stock, _Content);
    Wipe(password);

    if (!seat)
    {
        if (!SignsOut(seat.error()))
        {
            Refused(seat.error());
        }

        return;
    }

    _Seat = std::move(*seat);
    _InLobby = false;
    _NextPoll = Clock::now();
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Busy = false;
            seen.Room = MCMcoRoomSeen{
                .RoomId = _Seat->RoomId,
                .Name = name,
                .Hosting = true,
                .Players = {{.ClientId = _Peer.ClientId, .Username = _Peer.Username, .Ready = true, .Host = true}},
                .MaxPlayers = players,
                .Host = _Peer.Username};
        });
}

// Port-only (Redux HD; SPEC-mco-hosting.md): an internet game on MCO too, said only in the log.

void MCMcoRooms::HostBoth(MCMcoBoth both)
{
    Post([this, both = std::move(both)]() mutable { DoHostBoth(std::move(both)); });
}

void MCMcoRooms::DoHostBoth(MCMcoBoth both)
{
    if (_Seat || _Peer.PeerToken.empty() || !Prepared(true) || _Stock.empty())
    {
        SDL_Log("MechCommander Online: the internet game isn't hosted there: %s",
                _Seat                     ? "this game is in a room already"
                : _Peer.PeerToken.empty() ? "this game has no player there"
                : _Stock.empty()          ? "it names no build for its Redux rooms"
                                          : "it can't be reached, or the game's data can't be read");
        Wipe(both.Password);
        return;
    }

    const int32_t players = MCMcoApi::MatchCapacity(both.MaxPlayers);
    auto seat = _Service->HostRoom(_Peer, both.Name, players, both.Password, _Stock, _Content);
    Wipe(both.Password);

    if (!seat)
    {
        if (!SignsOut(seat.error()))
        {
            SDL_Log("MechCommander Online: hosting the internet game's room: %s",
                    MCMcoApi::Describe(seat.error()).c_str());
        }

        return;
    }

    SDL_Log("MechCommander Online: the internet game is hosted there too, as \"%s\"", both.Name.c_str());
    _Seat = std::move(*seat);
    _InLobby = false;
    _NextPoll = Clock::now();
    _DescribedMax = players;
    const std::string name = both.Name;
    _Both = std::move(both);
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.ClientId = _Peer.ClientId;
            seen.Room = MCMcoRoomSeen{
                .RoomId = _Seat->RoomId,
                .Name = name,
                .Hosting = true,
                .Players = {{.ClientId = _Peer.ClientId, .Username = _Peer.Username, .Ready = true, .Host = true}},
                .MaxPlayers = players,
                .Host = _Peer.Username,
                .Both = true};
        });
}

void MCMcoRooms::Describe(std::string description, int32_t maxPlayers)
{
    Post([this, description = std::move(description), maxPlayers] { DoDescribe(description, maxPlayers); });
}

void MCMcoRooms::DoDescribe(const std::string& description, int32_t maxPlayers)
{
    const int32_t players = MCMcoApi::MatchCapacity(maxPlayers);

    if (!_Seat || !_Seat->Host || !_Both || (description == _Described && players == _DescribedMax))
    {
        return;
    }

    if (const auto updated = _Service->UpdateRoom(_Peer, *_Seat, _Both->Name, description, players); !updated)
    {
        SDL_Log("MechCommander Online: describing the internet game's room: %s",
                MCMcoApi::Describe(updated.error()).c_str());
        return;
    }

    _Described = description;
    _DescribedMax = players;
    Show(
        [&](MCMcoSeen& seen)
        {
            if (seen.Room)
            {
                seen.Room->MaxPlayers = players;
            }
        });
}

void MCMcoRooms::Kick(std::string clientId)
{
    Post([this, clientId = std::move(clientId)] { DoKick(clientId); });
}

void MCMcoRooms::DoKick(const std::string& clientId)
{
    if (!_Seat || !_Seat->Host || !_Both)
    {
        SDL_Log("MechCommander Online: removing %s: this game doesn't host the internet game's room", clientId.c_str());
        return;
    }

    if (const auto kicked = _Service->KickPeer(_Peer, clientId); !kicked)
    {
        SDL_Log("MechCommander Online: removing %s from the internet game's room: %s", clientId.c_str(),
                MCMcoApi::Describe(kicked.error()).c_str());
    }
}

void MCMcoRooms::Sit(std::string roomId, std::string name, int32_t maxPlayers, std::string password)
{
    Post([this, roomId = std::move(roomId), name = std::move(name), maxPlayers,
          password = std::move(password)]() mutable { DoSit(roomId, name, maxPlayers, std::move(password)); });
}

void MCMcoRooms::DoSit(const std::string& roomId, const std::string& name, int32_t maxPlayers, std::string password)
{
    if (_Seat || _Peer.PeerToken.empty() || !Prepared(true) || _Stock.empty())
    {
        SDL_Log("MechCommander Online: no seat in the host's room: %s",
                _Seat ? "this game is in a room already" : "this game can't take one there now");
        Wipe(password);
        Show([&](MCMcoSeen& seen) { seen.SeatRoom = roomId; });
        return;
    }

    MCMcoApi::MCRoom room;
    room.RoomId = roomId;
    room.Name = name;
    room.Engine = std::string(MCMcoApi::ReduxEngine);
    room.EngineBuild = _Stock;
    room.MaxPlayers = MCMcoApi::MatchCapacity(maxPlayers);
    auto seat = _Service->JoinRoom(_Peer, room, password, _Content, true);
    Wipe(password);

    if (!seat)
    {
        if (!SignsOut(seat.error()))
        {
            SDL_Log("MechCommander Online: no seat in the host's room: %s", MCMcoApi::Describe(seat.error()).c_str());
        }

        Show([&](MCMcoSeen& seen) { seen.SeatRoom = roomId; });
        return;
    }

    _Seat = std::move(*seat);
    _Silent = true;
    _InLobby = false;
    _NextPoll = Clock::now();
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Seated = true;
            seen.ClientId = _Peer.ClientId;
            seen.SeatRoom = roomId;
        });
}

void MCMcoRooms::Unsit()
{
    Post(
        [this]
        {
            if (_Silent)
            {
                DoLeave();
            }
        });
}

void MCMcoRooms::LetSeatGo()
{
    _Seat.reset();
    _Both.reset();
    _Described.clear();
    _DescribedMax = 0;
    _Silent = false;
    _SilentMatch.clear();
    _NextPoll = Clock::now();
    Show(
        [](MCMcoSeen& seen)
        {
            seen.Room.reset();
            seen.Match = MCMcoMatchStage::None;
            seen.Seated = false;
        });
}

void MCMcoRooms::Leave()
{
    Post([this] { DoLeave(); });
}

void MCMcoRooms::LeaveBoth()
{
    Post(
        [this]
        {
            if (_Both.has_value())
            {
                DoLeave();
            }
        });
}

void MCMcoRooms::DoLeave()
{
    if (!_Seat)
    {
        return;
    }

    EndMatch(false);

    if (const auto left = _Service->LeaveRoom(_Peer); left)
    {
        // Leaving is registering back in the lobby.
        _InLobby = true;
    }
    else
    {
        SDL_Log("MechCommander Online: leaving the room: %s", MCMcoApi::Describe(left.error()).c_str());
    }

    LetSeatGo();
}

void MCMcoRooms::ClearLeft()
{
    Show([](MCMcoSeen& seen) { seen.Left.clear(); });
}

void MCMcoRooms::Say(std::string text)
{
    text = Trimmed(text, SayMost);

    if (text.empty())
    {
        return;
    }

    // Shown at once, as MCO's launcher shows it; MCO's copy takes its place (TakeMessages).
    std::string requestId = MCMcoApi::NewClientId();
    bool inRoom = false;
    Show(
        [&](MCMcoSeen& seen)
        {
            if (!seen.Room)
            {
                return;
            }

            inRoom = true;
            seen.Room->Chat.push_back(MCMcoChatLine{
                .Name = MCMcoApi::Printable(seen.Username), .Text = MCMcoApi::Printable(text), .RequestId = requestId});

            if (seen.Room->Chat.size() > ChatLinesKept)
            {
                seen.Room->Chat.erase(seen.Room->Chat.begin());
            }
        });

    if (inRoom)
    {
        Post([this, text = std::move(text), requestId = std::move(requestId)] { DoSay(text, requestId); });
    }
}

void MCMcoRooms::DoSay(const std::string& text, const std::string& requestId)
{
    if (!_Seat)
    {
        return;
    }

    const auto sent = _Service->SendMessage(_Peer, text, requestId);

    if (sent)
    {
        // MCO's copy, with its id, at once.
        _NextPoll = Clock::now();
        return;
    }

    if (SignsOut(sent.error()))
    {
        return;
    }

    SDL_Log("MechCommander Online: the room's chat line (%zu bytes) wasn't sent: %s", text.size(),
            MCMcoApi::Describe(sent.error()).c_str());

    // Unsent: the line gives way to why (MCO's own sentence, when it said one).
    const std::string why = RefusalText(sent.error());
    const std::string said = why == UnreachableText ? std::string(NotSentText) : why;
    Show(
        [&](MCMcoSeen& seen)
        {
            if (!seen.Room)
            {
                return;
            }

            const auto own = std::ranges::find_if(seen.Room->Chat, [&](const MCMcoChatLine& line)
                                                  { return line.Id == 0 && line.RequestId == requestId; });

            if (own != seen.Room->Chat.end())
            {
                *own = MCMcoChatLine{.Text = said};
            }
        });
}

void MCMcoRooms::TakeMessages(const std::vector<MCMcoApi::MCRoomMessage>& messages)
{
    if (messages.empty())
    {
        return;
    }

    Show(
        [&](MCMcoSeen& seen)
        {
            if (!seen.Room)
            {
                return;
            }

            std::vector<MCMcoChatLine>& chat = seen.Room->Chat;

            for (const MCMcoApi::MCRoomMessage& message : messages)
            {
                if (std::ranges::any_of(chat, [&](const MCMcoChatLine& line) { return line.Id == message.Id; }))
                {
                    continue;
                }

                std::optional<MCMcoChatLine> line = ChatLineOf(message);

                if (!line)
                {
                    continue;
                }

                // The player's own line, shown as it was said: MCO's copy takes its place.
                const auto own =
                    message.RequestId.empty()
                        ? chat.end()
                        : std::ranges::find_if(chat, [&](const MCMcoChatLine& said)
                                               { return said.Id == 0 && said.RequestId == message.RequestId; });

                if (own != chat.end())
                {
                    *own = std::move(*line);
                }
                else
                {
                    chat.push_back(std::move(*line));
                }
            }

            if (chat.size() > ChatLinesKept)
            {
                chat.erase(chat.begin(), chat.begin() + static_cast<std::ptrdiff_t>(chat.size() - ChatLinesKept));
            }
        });
}

std::optional<MCMcoChatLine> MCMcoRooms::ChatLineOf(const MCMcoApi::MCRoomMessage& message)
{
    MCMcoChatLine line{.Id = message.Id, .RequestId = message.RequestId};

    if (message.System)
    {
        const std::string player = MCMcoApi::Printable(message.EventPlayer);
        const auto said = std::ranges::find(EventsSaid, std::string_view(message.Event), &MCEventSaid::Event);

        if (!player.empty() && said != EventsSaid.end())
        {
            line.Text = std::vformat(said->Format, std::make_format_args(player));
        }
        else if (!player.empty() && message.EventKind == LaunchRequestedKind)
        {
            line.Text = std::format(LaunchRequestedFormat, player);
        }
        else if (message.EventKind == GameStartedKind)
        {
            line.Text = std::string(GameStartedText);
        }
        else
        {
            line.Text = MCMcoApi::Printable(message.Text);
        }

        return line.Text.empty() ? std::nullopt : std::optional<MCMcoChatLine>(std::move(line));
    }

    line.Name = MCMcoApi::Printable(message.Username);
    line.Text = MCMcoApi::Printable(message.Text);

    // A picture, a GIF or a voice message: what it was, said as the sender's.
    if (line.Text.empty())
    {
        const auto kind = std::ranges::find(KindsSaid, std::string_view(message.Kind), &MCKindSaid::Kind);

        if (kind == KindsSaid.end() || line.Name.empty())
        {
            return std::nullopt;
        }

        line.Text = std::format("{} {}", line.Name, kind->Said);
        line.Name.clear();
    }

    return line;
}

void MCMcoRooms::LeftRoom(std::string why)
{
    // Port-only (Redux HD): a guest's quiet seat, or the internet game's room, goes without a word on the screens.
    if (_Silent || _Both)
    {
        SDL_Log("MechCommander Online: out of the internet game's room: %s", why.c_str());
        EndMatch(false);
        _InLobby = false;
        LetSeatGo();
        return;
    }

    EndMatch(false);
    _Seat.reset();
    _InLobby = false;
    _NextPoll = Clock::now();
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Room.reset();
            seen.Match = MCMcoMatchStage::None;
            seen.Left = std::move(why);
        });
}

void MCMcoRooms::PollRoom()
{
    // A quiet seat's game is in its room's match on our links: it's reported running, so MCO keeps the seat.
    const bool running =
        _Silent ? !_SilentMatch.empty()
                : _Match.has_value() && _GameIn && ((_Guest && _Guest->Running()) || (_Hosted && _Hosted->Running()));
    const std::string session = _Silent ? _SilentMatch : _Match ? _Match->SessionHex : std::string();
    const auto state = _Service->RoomPoll(_Peer, *_Seat, session, running);
    std::string host;

    {
        const std::scoped_lock lock(_Lock);
        host = _Seen.Room ? _Seen.Room->Host : std::string();
    }

    if (!state)
    {
        if (SignsOut(state.error()))
        {
            return;
        }

        SDL_Log("MechCommander Online: the room's poll: %s", MCMcoApi::Describe(state.error()).c_str());

        // MCO unreachable or busy for a moment isn't the room gone: it's asked again.
        if (++_PollFailures >= PollFailuresMost)
        {
            _PollFailures = 0;
            LeftRoom(std::string(UnreachableText));
        }

        return;
    }

    _PollFailures = 0;

    if (state->Gone)
    {
        LeftRoom(LeftText(state->GoneWhy, host, _Match.has_value(), state->GoneBy));
        return;
    }

    // A quiet seat shows nothing and plays no match: its room's lines reach the game through its host.
    if (_Silent)
    {
        _SilentMatch = state->Start ? state->Start->SessionHex : std::string();
        return;
    }

    TakeMessages(state->Messages);

    Show(
        [&](MCMcoSeen& seen)
        {
            if (!seen.Room)
            {
                return;
            }

            seen.Room->Players = state->Players;

            if (!_Seat->RoomId.empty())
            {
                seen.Room->RoomId = _Seat->RoomId;
            }

            if (!state->Name.empty())
            {
                seen.Room->Name = state->Name;
            }

            if (const std::string named = HostOf(state->Players); !named.empty())
            {
                seen.Room->Host = named;
            }
        });

    // A guest plays the host's match once (its start signal stays while the match runs), trying again a few times
    // when the relay or the host wasn't ready for it.
    if (!_Seat->Host && state->Start && !_Match && state->Start->SessionHex != _Played &&
        (_MatchTries == 0 || Clock::now() - _MatchTried >= MatchRetryEvery))
    {
        _MatchTried = Clock::now();
        PlayAsGuest(*state->Start);

        if (_Match || ++_MatchTries >= MatchTries)
        {
            _Played = _Match ? std::string() : state->Start->SessionHex;
            _MatchTries = 0;
        }
    }
}

void MCMcoRooms::Start()
{
    Post([this] { DoStart(); });
}

void MCMcoRooms::DoStart()
{
    std::optional<MCMcoRoomSeen> room;

    {
        const std::scoped_lock lock(_Lock);
        room = _Seen.Room;
    }

    if (!_Seat || !_Seat->Host || _Match || !room || !Startable(*room))
    {
        return;
    }

    Show(
        [](MCMcoSeen& seen)
        {
            seen.Match = MCMcoMatchStage::Starting;
            seen.Refusal.clear();
        });
    const auto match = _Service->CreateSession(_Peer, _Stock, _Content);

    if (!match)
    {
        Show([](MCMcoSeen& seen) { seen.Match = MCMcoMatchStage::None; });
        Refused(match.error());
        return;
    }

    HostMatch(*match);
}

void MCMcoRooms::StartBoth(bool battle)
{
    // LET IN asks afresh: its last failure goes at once, so the room no longer says it (MCMcoSeats::LetInFailed).
    if (!battle)
    {
        Show([](MCMcoSeen& seen) { seen.Refusal.clear(); });
    }

    Post([this, battle] { DoStartBoth(battle); });
}

void MCMcoRooms::DoStartBoth(bool battle)
{
    if (!_Both || !_Seat || !_Seat->Host || _Match)
    {
        return;
    }

    std::optional<MCMcoRoomSeen> room;

    {
        const std::scoped_lock lock(_Lock);
        room = _Seen.Room;
    }

    // MCO's Start wants two players at least in its room, every one READY (its launcher's rule).
    if (!room || !Startable(*room))
    {
        if (battle)
        {
            OffForBattle("it has fewer than two players there, or one not READY");
        }
        else
        {
            Refused(std::string(LetInFailedText));
        }

        return;
    }

    Show(
        [](MCMcoSeen& seen)
        {
            seen.Match = MCMcoMatchStage::Starting;
            seen.Refusal.clear();
        });
    const auto match = _Service->CreateSession(_Peer, _Stock, _Content);

    if (!match)
    {
        Show([](MCMcoSeen& seen) { seen.Match = MCMcoMatchStage::None; });
        SDL_Log("MechCommander Online refused the internet game's START: %s",
                MCMcoApi::Describe(match.error()).c_str());

        if (battle)
        {
            OffForBattle("MCO refused its START");
        }
        else
        {
            Refused(std::string(LetInFailedText));
        }

        return;
    }

    HostMatch(*match);

    if (battle && !_Match)
    {
        OffForBattle("its relay couldn't be reached");
    }
}

void MCMcoRooms::BothMatchOver()
{
    Post(
        [this]
        {
            if (_Both && _Match)
            {
                EndMatch(true);
            }
        });
}

void MCMcoRooms::OffForBattle(std::string_view why)
{
    SDL_Log("MechCommander Online: the internet game's battle is off MCO: %.*s", static_cast<int>(why.size()),
            why.data());
    Show([](MCMcoSeen& seen) { seen.Refusal.clear(); });
    DoLeave();
}

// The match.

void MCMcoRooms::HostMatch(const MCMcoApi::MCMatch& match)
{
    // Port-only (Redux HD): the internet game's room is already hosted, on its own port in its own network.
    const bool both = _Both.has_value();
    const auto callOff = [&](std::string why)
    {
        if (const auto closed = _Service->CloseSession(_Peer, match.SessionHex); !closed)
        {
            SDL_Log("MechCommander Online: calling the match off: %s", MCMcoApi::Describe(closed.error()).c_str());
        }

        _Hosted.reset();
        _Match.reset();
        SDL_Log("MechCommander Online: the match couldn't start: %s", why.c_str());
        Show(
            [&](MCMcoSeen& seen)
            {
                seen.Match = MCMcoMatchStage::None;
                seen.GamePort = 0;
                seen.Refusal = std::string(both ? LetInFailedText : NoMatchText);
            });
    };

    const MCMcoBridge::MCDial dial = _Setup.Dial(match);
    const auto epoch = MCMcoBridge::Probe(dial);

    if (!epoch)
    {
        callOff(std::format("the relay's epoch: {}", epoch.error()));
        return;
    }

    const uint16_t port = both ? _Both->GamePort : _Setup.GamePort();

    if (port == 0)
    {
        callOff("no free port for the game");
        return;
    }

    _Cancel = false;
    _Match = match;

    if (!both)
    {
        Show(
            [&](MCMcoSeen& seen)
            {
                seen.Match = MCMcoMatchStage::HostGame;
                seen.GamePort = port;
                seen.SessionName = MCMcoApi::MatchSession(match.SessionHex);
                seen.Capacity = MCMcoApi::MatchCapacity(match.Capacity);
                seen.MatchName = MCMcoApi::MatchName(_Peer.Username);
            });
    }

    // The game hosts on the port (the screens do it): its answer to the session query is awaited, as the launcher's.
    if (!both && !MCMcoBridge::WaitForHostGame(port, _Setup.HostGameMs, _Cancel))
    {
        callOff(_Cancel ? "the game couldn't host it" : "the game didn't answer on its port");
        return;
    }

    const auto ticket = _Service->Ticket(_Peer, match, *epoch, true);

    if (!ticket)
    {
        callOff(MCMcoApi::Describe(ticket.error()));
        return;
    }

    const auto note = [](std::string_view said)
    { SDL_Log("MechCommander Online's relay: %.*s", static_cast<int>(said.size()), said.data()); };
    auto hosted = both ? MCMcoBridge::MCHost::Start(dial, ticket->Ticket, port, _Both->Carry, note)
                       : MCMcoBridge::MCHost::Start(dial, ticket->Ticket, port, note);

    if (!hosted)
    {
        callOff(hosted.error());
        return;
    }

    _Hosted = std::move(*hosted);

    // A match MCO wasn't told of can't be joined: it's called off, as the launcher calls it off.
    if (const auto published = _Service->PublishReady(_Peer, match.SessionHex); !published)
    {
        callOff(MCMcoApi::Describe(published.error()));
        return;
    }

    _GameIn = true;
    Show([](MCMcoSeen& seen) { seen.Match = MCMcoMatchStage::Playing; });
}

void MCMcoRooms::PlayAsGuest(const MCMcoApi::MCMatch& match)
{
    // As MCO's launcher holds a match to the player's data before starting its game.
    if (!MCMcoApi::SameId(match.EngineContent, _Content))
    {
        _Played = match.SessionHex;
        Refused(std::string(OtherDataText));
        return;
    }

    Show(
        [](MCMcoSeen& seen)
        {
            seen.Match = MCMcoMatchStage::Starting;
            seen.Refusal.clear();
        });
    const auto failed = [&](std::string why)
    {
        SDL_Log("MechCommander Online: joining the match: %s", why.c_str());

        if (SaysSessionGone(why))
        {
            if (const auto reported = _Service->ReportSessionMissing(_Peer, match.SessionHex); !reported)
            {
                SDL_Log("MechCommander Online: telling MCO the match's session had gone: %s",
                        MCMcoApi::Describe(reported.error()).c_str());
            }
        }

        Show([](MCMcoSeen& seen) { seen.Match = MCMcoMatchStage::None; });
    };

    const MCMcoBridge::MCDial dial = _Setup.Dial(match);
    const auto epoch = MCMcoBridge::Probe(dial);

    if (!epoch)
    {
        failed(std::format("the relay's epoch: {}", epoch.error()));
        return;
    }

    const auto ticket = _Service->Ticket(_Peer, match, *epoch, false);

    if (!ticket)
    {
        failed(ticket.error().Message);
        return;
    }

    auto guest = MCMcoBridge::MCGuest::Start(
        dial, ticket->Ticket, [](std::string_view note)
        { SDL_Log("MechCommander Online's relay: %.*s", static_cast<int>(note.size()), note.data()); });

    if (!guest)
    {
        failed(guest.error());
        return;
    }

    _Guest = std::move(*guest);
    _Match = match;
    _GameIn = false;
    _Cancel = false;
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Match = MCMcoMatchStage::JoinGame;
            seen.GamePort = _Guest->Port();
            seen.Capacity = MCMcoApi::MatchCapacity(match.Capacity);
            seen.MatchName = MCMcoApi::MatchName(_Peer.Username);
        });
}

void MCMcoRooms::GameFailed()
{
    _Cancel = true;
    Post(
        [this]
        {
            if (_Match && _Seat && !_Seat->Host)
            {
                _Played = _Match->SessionHex;
            }

            EndMatch(false);
            Refused(std::string(NoMatchText));
        });
}

void MCMcoRooms::GameInMatch()
{
    Post(
        [this]
        {
            if (!_Match)
            {
                return;
            }

            _GameIn = true;
            Show([](MCMcoSeen& seen) { seen.Match = MCMcoMatchStage::Playing; });
        });
}

void MCMcoRooms::GameEnded()
{
    Post(
        [this]
        {
            if (_Match && _Seat && !_Seat->Host)
            {
                _Played = _Match->SessionHex;
            }

            EndMatch(_GameIn);
        });
}

void MCMcoRooms::EndMatch(bool played)
{
    if (_Guest)
    {
        _Guest->Stop();
        _Guest.reset();
    }

    if (_Hosted)
    {
        _Hosted->Stop();
        _Hosted.reset();
    }

    // The host's match closed: one its game came back from with the room's engine, one called off without.
    if (_Match && _Seat && _Seat->Host)
    {
        const auto closed =
            played ? _Service->CloseSession(_Peer, _Match->SessionHex, _Match->EngineBuild, _Match->EngineContent)
                   : _Service->CloseSession(_Peer, _Match->SessionHex);

        if (!closed)
        {
            SDL_Log("MechCommander Online: closing the match: %s", MCMcoApi::Describe(closed.error()).c_str());
        }
    }

    _Match.reset();
    _GameIn = false;
    Show(
        [](MCMcoSeen& seen)
        {
            seen.Match = MCMcoMatchStage::None;
            seen.GamePort = 0;
        });
}

// What the screens read.

MCMcoSeen MCMcoRooms::Seen() const
{
    const std::scoped_lock lock(_Lock);
    return _Seen;
}

uint64_t MCMcoRooms::Revision() const
{
    const std::scoped_lock lock(_Lock);
    return _Seen.Revision;
}

void MCMcoRooms::Refused(const MCMcoApi::MCError& error)
{
    SDL_Log("MechCommander Online refused: %s", MCMcoApi::Describe(error).c_str());
    Refused(RefusalText(error));
}

void MCMcoRooms::Refused(std::string text)
{
    Show(
        [&](MCMcoSeen& seen)
        {
            seen.Busy = false;
            seen.Doing.clear();
            seen.Refusal = std::move(text);

            if (seen.Account == MCMcoAccount::OtherComputer)
            {
                seen.Account = MCMcoAccount::SignedOut;
            }
        });
}

bool MCMcoRooms::Startable(const MCMcoRoomSeen& room)
{
    return static_cast<int32_t>(room.Players.size()) >= MCMcoApi::MatchFewest &&
           std::ranges::all_of(room.Players,
                               [](const MCMcoApi::MCRoomPlayer& player) { return player.Ready || player.Host; });
}

std::string MCMcoRooms::RefusalText(const MCMcoApi::MCError& error)
{
    if (error.Unreachable || error.Status >= ServerErrorStatus)
    {
        return std::string(UnreachableText);
    }

    if (error.Code == MCMcoApi::InvalidResponseCode)
    {
        return std::string(ChangedText);
    }

    if (error.Code == WrongSignInCode)
    {
        return std::string(WrongSignInText);
    }

    if (std::ranges::find(AccountLimitCodes, error.Code) != AccountLimitCodes.end())
    {
        return std::string(ThreeAccountsText);
    }

    if (error.Status == TooManyStatus)
    {
        return std::string(TooManyText);
    }

    const std::string text = MCMcoApi::Printable(error.Message);
    return text.empty() ? std::string(UnreachableText) : text;
}

std::string MCMcoRooms::LeftText(std::string_view why, std::string_view host, bool inMatch, std::string_view by)
{
    const std::string named = host.empty() ? std::string("The host") : MCMcoApi::Printable(host);

    if (why == "host_connection_lost" || why == "closed" || why == "host_closed")
    {
        return inMatch ? std::format(MatchEndedFormat, named) : std::format(RoomClosedFormat, named);
    }

    if (why == "kicked")
    {
        const std::string remover = MCMcoApi::Printable(by);
        return std::format(RemovedFormat, remover.empty() ? named : remover);
    }

    if (why == "moderator_closed")
    {
        return "A moderator closed the room.";
    }

    return "You're no longer in the room.";
}
