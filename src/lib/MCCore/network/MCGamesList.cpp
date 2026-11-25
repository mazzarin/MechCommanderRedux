#include "stdafx.h"
#include "network/MCGamesList.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "main/MCUpdater.h"
#include "network/MCInternetGame.h"
#include "network/MCMultiPlayer.h"

#include <charconv>

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>
    /// Sessions dropped from the lists because they had no players (never listed again; the 1998 game list's, which
    /// shared them).
    /// </summary>
    std::vector<_GUID>& DeletedSessions()
    {
        static std::vector<_GUID> deleted;
        return deleted;
    }

    /// <summary>Whether <paramref name="session"/> was dropped from the lists (it had no players).</summary>
    bool IsSessionDeleted(const MCFidpSession& session)
    {
        return std::ranges::any_of(DeletedSessions(), [&](const _GUID& deleted)
                                   { return MCSameGuid(session.SessionDesc.guidInstance, deleted); });
    }

    /// <summary>
    /// Asks for sessions without listing them, so the first read of a list just shown lists those that answered (a read
    /// doesn't wait for answers: they're listed by the next).
    /// </summary>
    void AskForSessions()
    {
        const std::shared_ptr<MCMultiPlayer> multiPlayer = MultiPlayer();

        if (multiPlayer != nullptr && multiPlayer->SessionManager != nullptr)
        {
            multiPlayer->SessionManager->GetSessions(false);
        }
    }

    /// <summary>This network's games read again every two seconds, as the 1998 LAN screen's poll did.</summary>
    constexpr auto LanReadEvery = std::chrono::milliseconds(2000);
    /// <summary>A guest's search for its host's room over the link: every half second, for ten seconds.</summary>
    constexpr auto JoinEvery = std::chrono::milliseconds(500);
    constexpr int32_t JoinTries = 20;
    constexpr int64_t SecondsPerMinute = 60;

    constexpr std::string_view OpenText = "Open";
    constexpr std::string_view FullText = "Full";
    constexpr std::string_view InBattleText = "In battle";
    constexpr std::string_view InBattleForFormat = "In battle for {} min";
    constexpr std::string_view NeedsFormat = "Needs version {}";
    constexpr std::string_view PlayersFormat = "{}/{}";

    // The joins' refusals (SPEC-menus.md, "When something goes wrong").
    constexpr std::string_view FullFormat = "{} is full.";
    constexpr std::string_view StartedFormat = "{} has started.";
    constexpr std::string_view ClosedFormat = "{} has closed.";
    constexpr std::string_view NoCodeFormat = "No game has the code {}.";
    constexpr std::string_view UnreachableFormat = "Couldn't connect to {}.";
    constexpr std::string_view NotHostedText = "Couldn't set up your game.";
    constexpr std::string_view UpdateText = "Update to play online.";
    constexpr std::string_view TooManyText = "Too many tries. Wait a minute.";
    constexpr std::string_view CrowdedText = "Too many players on your network are in games.";
    constexpr std::string_view BannedText = "This copy is blocked from online games.";
    constexpr std::string_view NoInternetText = "This version can't play online.";
    constexpr std::string_view LacksFormat = "Your game doesn't have this match's {}. Update your game to join.";
    /// <summary>What a host's refusal names when it names nothing.</summary>
    constexpr std::string_view LacksNothing = "options";

    /// <summary>How the macOS and Windows sockets say there's no way to the internet (MCWebSocket::WhyClosed).</summary>
    constexpr std::array<std::string_view, 3> OfflineMarks = {
        "offline",             // NSURLErrorNotConnectedToInternet's words
        "could not be found",  // NSURLErrorCannotFindHost's: the service's name not looked up
        "WinHTTP error 12007", // ERROR_WINHTTP_NAME_NOT_RESOLVED
    };

    /// <summary>The continents Cloudflare names a host's location by, as the list says them.</summary>
    constexpr std::array<std::pair<std::string_view, std::string_view>, 7> Regions{{
        {"AF", "Africa"},
        {"AN", "Antarctica"},
        {"AS", "Asia"},
        {"EU", "Europe"},
        {"NA", "N. America"},
        {"OC", "Oceania"},
        {"SA", "S. America"},
    }};

    /// <summary>A version's release ("0.6.0" of "0.6.0 (812)") as numbers; none when it isn't one.</summary>
    std::optional<std::vector<int32_t>> ReleaseOf(std::string_view version)
    {
        const std::string_view release = version.substr(0, version.find(' '));
        std::vector<int32_t> parts;
        size_t at = 0;

        while (at <= release.size())
        {
            const size_t dot = std::min(release.find('.', at), release.size());
            int32_t part = 0;
            const auto [end, error] = std::from_chars(release.data() + at, release.data() + dot, part);

            if (error != std::errc() || end != release.data() + dot)
            {
                return std::nullopt;
            }

            parts.push_back(part);
            at = dot + 1;
        }

        return parts;
    }

    /// <summary>A LAN session's instance as text, the list's key for it.</summary>
    std::string KeyOf(const _GUID& guid)
    {
        std::string key = std::format("{:08x}-{:04x}-{:04x}-", guid.Data1, guid.Data2, guid.Data3);

        for (const uint8_t byte : guid.Data4)
        {
            key += std::format("{:02x}", byte);
        }

        return key;
    }

    MCSessionManager* Manager()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        return game != nullptr ? game->SessionManager.get() : nullptr;
    }

    /// <summary>A host's refusal at the door (the options it lacks, in the host's capitals) in a sentence.</summary>
    std::string LacksText(std::string_view options)
    {
        std::string named(options);
        std::ranges::transform(named, named.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        std::erase_if(named, [](unsigned char c) { return c < ' ' || c > '~'; });
        return std::format(LacksFormat, named.empty() ? std::string(LacksNothing) : named);
    }
}

std::string MCListedGame::PlayersText() const
{
    return std::format(PlayersFormat, Players.size(), Max);
}

std::string MCListedGame::Release() const
{
    return Version.substr(0, Version.find(' '));
}

std::string MCListedGame::NowText() const
{
    if (NeedsNewer)
    {
        return std::format(NeedsFormat, Release());
    }

    if (InBattle)
    {
        const int64_t minutes = BattleSeconds.value_or(0) / SecondsPerMinute;
        return minutes > 0 ? std::format(InBattleForFormat, minutes) : std::string(InBattleText);
    }

    return std::string(Full() ? FullText : OpenText);
}

namespace
{
    /// <summary>Whether release <paramref name="version"/> is newer than <paramref name="ours"/> (none: never).</summary>
    bool NewerThan(std::string_view version, const std::optional<std::vector<int32_t>>& ours)
    {
        const std::optional<std::vector<int32_t>> theirs = ReleaseOf(version);
        return theirs.has_value() && ours.has_value() && *theirs > *ours;
    }
}

bool MCNeedsNewerVersion(std::string_view version)
{
    return NewerThan(version, ReleaseOf(MCInternetGame::SetupNow().Version));
}

std::string_view MCRegionName(std::string_view region)
{
    const auto found = std::ranges::find(Regions, region, &std::pair<std::string_view, std::string_view>::first);
    return found != Regions.end() ? found->second : std::string_view();
}

bool MCSocketOffline(std::string_view whyClosed)
{
    return std::ranges::any_of(OfflineMarks, [whyClosed](std::string_view mark)
                               { return whyClosed.find(mark) != std::string_view::npos; });
}

// ---- the list ----

MCGamesList::MCGamesList(MCGamesWhere where) : _Where(where)
{
}

MCGamesList::~MCGamesList() = default;

void MCGamesList::Open()
{
    _Open = true;
    _Games.clear();

    if (_Where == MCGamesWhere::Internet)
    {
        _List = MCInternetList::Open();
        return;
    }

    // This network asked now; its answers are read by the next read (a read doesn't wait for answers).
    _LanListed = false;
    AskForSessions();
    _NextLanRead = Clock::now() + LanReadEvery;
}

void MCGamesList::Close()
{
    _Open = false;
    // Its socket's close takes it off the service's count.
    _List.reset();
}

void MCGamesList::Retry()
{
    if (!_Open)
    {
        return;
    }

    if (_Where == MCGamesWhere::Internet)
    {
        SDL_Log("Games list: RETRY: the service's list opened again");
        _List = MCInternetList::Open();
        _Games.clear();
        return;
    }

    AskForSessions();
    _NextLanRead = Clock::now() + LanReadEvery;
}

bool MCGamesList::Update()
{
    if (!_Open)
    {
        return false;
    }

    if (_Where == MCGamesWhere::Internet)
    {
        const bool changed = _List != nullptr && _List->Update();
        return ReadInternetGames() || changed;
    }

    if (Clock::now() < _NextLanRead)
    {
        return false;
    }

    _NextLanRead = Clock::now() + LanReadEvery;
    const bool wasListed = _LanListed;
    _LanListed = true;
    return ReadLanGames() || !wasListed;
}

bool MCGamesList::ReadLanGames()
{
    MCSessionManager* manager = Manager();
    std::vector<MCListedGame> games;

    if (manager != nullptr)
    {
        // The sessions that have players (an empty one is remembered as gone and never listed again; the scan stops at
        // the first such), as the 1998 LAN screen's list kept them, each with its players read before joining.
        const std::optional<std::vector<std::shared_ptr<MCFidpSession>>> sessions = manager->GetSessions(false);

        for (size_t i = 0; sessions.has_value() && i < sessions->size(); i++)
        {
            const std::shared_ptr<MCFidpSession>& session = (*sessions)[i];

            if (IsSessionDeleted(*session))
            {
                break;
            }

            const std::vector<std::shared_ptr<MCFidpPlayer>>& players = manager->GetPlayers(session);

            if (players.empty())
            {
                DeletedSessions().push_back(session->SessionDesc.guidInstance);
                continue;
            }

            MCListedGame game{.Key = KeyOf(session->SessionDesc.guidInstance),
                              .Name = session->Name,
                              .Max = static_cast<int32_t>(session->SessionDesc.dwMaxPlayers),
                              .InBattle = (session->SessionDesc.dwFlags &
                                           (DPSESSION_NEWPLAYERSDISABLED | DPSESSION_JOINDISABLED)) != 0};

            for (const std::shared_ptr<MCFidpPlayer>& player : players)
            {
                game.Players.push_back(player->Name);
            }

            games.push_back(std::move(game));
        }
    }

    // Open games first, then those in battle, each in the order found.
    std::ranges::stable_partition(games, [](const MCListedGame& game) { return !game.InBattle; });

    if (games == _Games)
    {
        return false;
    }

    _Games = std::move(games);
    return true;
}

bool MCGamesList::ReadInternetGames()
{
    std::vector<MCListedGame> games;

    if (_List != nullptr && _List->Listing().has_value() && !_List->Listing()->Games.empty())
    {
        // This build's release read once for the listing, not for each game (the setup is read under its lock).
        const std::optional<std::vector<int32_t>> ours = ReleaseOf(MCInternetGame::SetupNow().Version);

        for (const MCInternetListing::Game& listed : _List->Listing()->Games)
        {
            games.push_back(MCListedGame{.Key = listed.Code,
                                         .Name = listed.Name,
                                         .Players = listed.Players,
                                         .Max = listed.Max,
                                         .Region = std::string(MCRegionName(listed.Region)),
                                         .Mission = listed.Mission,
                                         .Version = listed.Version,
                                         .InBattle = listed.BattleSeconds.has_value(),
                                         .BattleSeconds = listed.BattleSeconds,
                                         .NeedsNewer = NewerThan(listed.Version, ours)});
        }
    }

    if (games == _Games)
    {
        return false;
    }

    _Games = std::move(games);
    return true;
}

MCGamesState MCGamesList::State() const
{
    if (_Where == MCGamesWhere::Lan)
    {
        return _LanListed ? MCGamesState::Listed : MCGamesState::Looking;
    }

    if (_List == nullptr)
    {
        return _Open && !MCInternetGame::Available() ? MCGamesState::Unavailable : MCGamesState::Looking;
    }

    if (const std::optional<MCServiceRefusal> refusal = _List->Refusal(); refusal.has_value())
    {
        return *refusal == MCServiceRefusal::Update ? MCGamesState::Update : MCGamesState::Blocked;
    }

    if (_List->Listing().has_value())
    {
        return MCGamesState::Listed;
    }

    if (const std::optional<std::string> trouble = _List->Trouble(); trouble.has_value())
    {
        return MCSocketOffline(*trouble) ? MCGamesState::Offline : MCGamesState::NotAnswering;
    }

    return MCGamesState::Looking;
}

std::optional<MCListedGame> MCGamesList::Find(std::string_view key) const
{
    const auto found = std::ranges::find(_Games, key, &MCListedGame::Key);
    return found != _Games.end() ? std::optional(*found) : std::nullopt;
}

std::optional<std::pair<int32_t, int32_t>> MCGamesList::Counts() const
{
    if (_List == nullptr || !_List->Listing().has_value())
    {
        return std::nullopt;
    }

    return std::pair{_List->Listing()->Online, _List->Listing()->InGames};
}

// ---- joining and hosting ----

bool MCGameJoin::Begin(MCAsk ask)
{
    _Ask = std::move(ask);
    _End = MCEnd::None;
    _Refusal.clear();
    _CanRetry = false;
    _NeedsUpdate = false;
    _Searches = 0;
    _NextSearch = Clock::now();
    MCSessionManager* manager = Manager();

    if (manager == nullptr)
    {
        Refuse(_Ask->Hosting ? std::string(NotHostedText) : std::format(UnreachableFormat, _Ask->Shown), true);
        return false;
    }

    if (_Ask->Where == MCGamesWhere::Lan)
    {
        SDL_Log("Games: %s %s on this network", _Ask->Hosting ? "hosting" : "joining", _Ask->Shown.c_str());
        return true;
    }

    // The connection is chosen first (choosing it again would leave the session, and the game with it); a guest's
    // address is its game's code, as the links name it. A public game (a host's choice, or a guest's game from the
    // service's list) takes relays only.
    manager->ConnectTcp(_Ask->Hosting ? std::string_view() : std::string_view(_Ask->Key));
    const bool isPublic = _Ask->Hosting ? _Ask->Public : !_Ask->Typed;

    if (!manager->BeginInternet(_Ask->Hosting, _Ask->Hosting ? std::string_view() : std::string_view(_Ask->Key),
                                isPublic))
    {
        Refuse(std::string(NoInternetText));
        return false;
    }

    SDL_Log("Games: %s %s over the internet", _Ask->Hosting ? "hosting" : "joining", _Ask->Shown.c_str());
    return true;
}

void MCGameJoin::Refuse(std::string text, bool canRetry, bool needsUpdate)
{
    SDL_Log("Games: %s %s ended: %s", _Ask.has_value() && _Ask->Hosting ? "hosting" : "joining",
            _Ask.has_value() ? _Ask->Shown.c_str() : "a game", text.c_str());
    _Refusal = std::move(text);
    _CanRetry = canRetry;
    _NeedsUpdate = needsUpdate;
    _End = MCEnd::Refused;

    if (MCSessionManager* manager = Manager();
        manager != nullptr && _Ask.has_value() && _Ask->Where == MCGamesWhere::Internet)
    {
        manager->EndInternet();
    }
}

void MCGameJoin::RefuseFor(std::optional<MCServiceRefusal> refusal)
{
    const MCAsk& ask = *_Ask;

    switch (refusal.value_or(MCServiceRefusal::Unreachable))
    {
        case MCServiceRefusal::NoGame:
        case MCServiceRefusal::GameOver:
        {
            Refuse(ask.Typed ? std::format(NoCodeFormat, ask.Key) : std::format(ClosedFormat, ask.Shown));
            return;
        }
        case MCServiceRefusal::Full:
        {
            Refuse(std::format(FullFormat, ask.Shown));
            return;
        }
        case MCServiceRefusal::InBattle:
        {
            Refuse(std::format(StartedFormat, ask.Shown));
            return;
        }
        case MCServiceRefusal::Update:
        {
            Refuse(std::string(UpdateText), false, true);
            return;
        }
        case MCServiceRefusal::TooManyTries:
        {
            Refuse(std::string(TooManyText));
            return;
        }
        case MCServiceRefusal::Banned:
        {
            Refuse(std::string(BannedText));
            return;
        }
        case MCServiceRefusal::Crowded:
        {
            Refuse(std::string(CrowdedText));
            return;
        }

        // Anything else the service refused reads as the game not reached or not set up.
        default:
        {
            Refuse(ask.Hosting ? std::string(NotHostedText) : std::format(UnreachableFormat, ask.Shown), true);
            return;
        }
    }
}

auto MCGameJoin::Tend() -> MCEnd
{
    if (!Pending())
    {
        return _End;
    }

    _End = _Ask->Where == MCGamesWhere::Lan ? TendLan() : TendInternet();
    return _End;
}

auto MCGameJoin::TendLan() -> MCEnd
{
    MCSessionManager* manager = Manager();
    const MCAsk& ask = *_Ask;

    if (ask.Hosting)
    {
        if (MultiPlayer()->CreateSession(ask.GameName, ask.PlayerName, ask.MaxPlayers) != 0)
        {
            Refuse(std::string(NotHostedText), true);
            return _End;
        }

        return MCEnd::Joined;
    }

    // The session as last heard (the list's read doesn't wait for answers, nor does this).
    const std::optional<std::vector<std::shared_ptr<MCFidpSession>>> sessions = manager->GetSessions(false);
    std::shared_ptr<MCFidpSession> room;

    for (size_t i = 0; sessions.has_value() && i < sessions->size() && room == nullptr; i++)
    {
        if (KeyOf((*sessions)[i]->SessionDesc.guidInstance) == ask.Key)
        {
            room = (*sessions)[i];
        }
    }

    if (room == nullptr)
    {
        Refuse(std::format(ClosedFormat, ask.Shown));
        return _End;
    }

    if (room->SessionDesc.dwCurrentPlayers >= room->SessionDesc.dwMaxPlayers)
    {
        Refuse(std::format(FullFormat, ask.Shown));
        return _End;
    }

    if (manager->JoinSession(room->SessionDesc.guidInstance, ask.PlayerName) == 0)
    {
        NumLanPlayers = static_cast<int32_t>(manager->GetPlayers(nullptr).size());
        return MCEnd::Joined;
    }

    // The host refused this game at the door, saying why, or the room can't be joined.
    if (const std::string refusal = manager->RefusalReason(); !refusal.empty())
    {
        Refuse(LacksText(refusal));
        return _End;
    }

    switch (manager->LastRoomRefusal)
    {
        case MCRoomRefusal::Full:
            Refuse(std::format(FullFormat, ask.Shown));
            break;
        case MCRoomRefusal::Started:
            Refuse(std::format(StartedFormat, ask.Shown));
            break;
        default:
            Refuse(std::format(UnreachableFormat, ask.Shown), true);
            break;
    }

    return _End;
}

auto MCGameJoin::TendInternet() -> MCEnd
{
    MCSessionManager* manager = Manager();

    if (manager == nullptr || manager->Internet == nullptr)
    {
        RefuseFor(std::nullopt);
        return _End;
    }

    const MCInternetGame::Stage stage = manager->Internet->Update();

    if (stage == MCInternetGame::Stage::Failed)
    {
        RefuseFor(manager->Internet->Refusal());
        return _End;
    }

    if (stage != MCInternetGame::Stage::Ready)
    {
        return MCEnd::None;
    }

    const MCAsk& ask = *_Ask;

    if (ask.Hosting)
    {
        if (MultiPlayer()->CreateSession(ask.GameName, ask.PlayerName, ask.MaxPlayers) != 0)
        {
            Refuse(std::string(NotHostedText), true);
            return _End;
        }

        return MCEnd::Joined;
    }

    // A guest's link is open: its host's room is looked for over it, then joined.
    const Clock::time_point now = Clock::now();

    if (now < _NextSearch)
    {
        return MCEnd::None;
    }

    _NextSearch = now + JoinEvery;

    if (++_Searches > JoinTries)
    {
        RefuseFor(std::nullopt);
        return _End;
    }

    const std::optional<std::vector<std::shared_ptr<MCFidpSession>>> rooms = manager->GetSessions();

    if (!rooms.has_value() || rooms->empty())
    {
        return MCEnd::None;
    }

    const MCFidpSession& room = *rooms->front();

    if (room.SessionDesc.dwCurrentPlayers >= room.SessionDesc.dwMaxPlayers)
    {
        Refuse(std::format(FullFormat, ask.Shown));
        return _End;
    }

    if (manager->JoinSession(room.SessionDesc.guidInstance, ask.PlayerName) == 0)
    {
        NumLanPlayers = static_cast<int32_t>(manager->GetPlayers(nullptr).size());
        return MCEnd::Joined;
    }

    // The host refused this game at the door, saying why, or the room can't be joined.
    const std::string refusal = manager->RefusalReason();
    Refuse(!refusal.empty() ? LacksText(refusal) : std::format(UnreachableFormat, ask.Shown), refusal.empty());
    return _End;
}

void MCGameJoin::CallOff()
{
    if (!Pending())
    {
        return;
    }

    SDL_Log("Games: %s %s called off", _Ask->Hosting ? "hosting" : "joining", _Ask->Shown.c_str());
    _End = MCEnd::CalledOff;

    if (MCSessionManager* manager = Manager(); manager != nullptr && _Ask->Where == MCGamesWhere::Internet)
    {
        manager->EndInternet();
    }
}
