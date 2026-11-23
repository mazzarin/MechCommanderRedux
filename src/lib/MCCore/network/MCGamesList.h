#pragma once

// Port-only (SPEC-menus.md's stage 3, "Models and views"): the games list, which LAN and INTERNET view: the games on
// this network (DirectPlay's sessions, each with its players, read before joining) or the service's public games (its
// list socket, its counts and the week's busiest hours), each game as the screens show it; and joining or hosting one,
// a job that moves along a frame at a time while the screen stays live and can be called off. Pulled out of the 1998
// screens (the LAN screen, and the INTERNET screen in its layout) as they were remade. Every message from the service
// is checked before it's used (network/MCInternetLobby).

#include "network/MCInternetLobby.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// <summary>A game in the list, as LAN and INTERNET show it.</summary>
struct MCListedGame
{
    /// <summary>The service's code for an internet game; a LAN session's instance, as text.</summary>
    std::string Key;
    /// <summary>Its name as its host typed it.</summary>
    std::string Name;
    /// <summary>Its players' names, the host first.</summary>
    std::vector<std::string> Players;
    /// <summary>The most players its host takes.</summary>
    int32_t Max = 0;
    /// <summary>Where its host is ("Europe"); empty on this network or when the service didn't say.</summary>
    std::string Region;
    /// <summary>The mission its host picked; empty for none.</summary>
    std::string Mission;
    /// <summary>The host's version as its build shows it; empty on this network.</summary>
    std::string Version;
    /// <summary>In battle (a LAN game closed to new players, an internet game the service says is).</summary>
    bool InBattle = false;
    /// <summary>How long it has been in battle, when the service says.</summary>
    std::optional<int64_t> BattleSeconds;
    /// <summary>Its host plays a newer release than this game's (it can't be joined without the update).</summary>
    bool NeedsNewer = false;

    /// <summary>Whether every place is taken.</summary>
    bool Full() const { return Max > 0 && static_cast<int32_t>(Players.size()) >= Max; }
    /// <summary>Whether it can be joined as it stands: open, a place free, on a version this game plays.</summary>
    bool Joinable() const { return !InBattle && !Full() && !NeedsNewer; }
    /// <summary>Its players as the list shows them: "2/6".</summary>
    std::string PlayersText() const;
    /// <summary>
    /// What it's doing, the list's last column: "Open", "Full", "In battle", "In battle for 12 min", "Needs version 0.7.2".
    /// </summary>
    std::string NowText() const;
    /// <summary>The host's release alone ("0.7.2" of "0.7.2 (901)").</summary>
    std::string Release() const;

    bool operator==(const MCListedGame&) const = default;
};

/// <summary>Where a list's games are.</summary>
enum class MCGamesWhere
{
    /// <summary>On this network (DirectPlay's sessions, over TCP/IP).</summary>
    Lan,
    /// <summary>The service's public games.</summary>
    Internet
};

/// <summary>What the list has to show.</summary>
enum class MCGamesState
{
    /// <summary>Nothing heard yet: "Looking for games...".</summary>
    Looking,
    /// <summary>The service's list couldn't be reached for want of a connection: "No internet connection.".</summary>
    Offline,
    /// <summary>The service's list didn't answer: "The games list isn't answering.".</summary>
    NotAnswering,
    /// <summary>The service speaks a newer protocol: "Update to play online.".</summary>
    Update,
    /// <summary>The service turned this copy away: "This copy is blocked from online games.".</summary>
    Blocked,
    /// <summary>This build has no internet games (no service set).</summary>
    Unavailable,
    /// <summary>The list is in (empty or not).</summary>
    Listed
};

/// <summary>Whether <paramref name="version"/> (as a build shows it) is a newer release than this game's.</summary>
bool MCNeedsNewerVersion(std::string_view version);

/// <summary>A continent as Cloudflare names it ("EU"), as the list says it ("Europe"); empty for one it doesn't know.</summary>
std::string_view MCRegionName(std::string_view region);

/// <summary>
/// Whether a socket's reason for closing (MCWebSocket::WhyClosed) says this machine has no way to the internet (no
/// connection, the service's name not found), rather than the service not answering.
/// </summary>
bool MCSocketOffline(std::string_view whyClosed);

/// <summary>
/// The games, kept up while a screen shows them: <see cref="Open"/> as it shows (the service's list socket opened, or
/// this network asked for its games), <see cref="Update"/> each frame, <see cref="Close"/> as it goes (the socket's
/// close takes this copy off the service's count).
/// </summary>
class MCGamesList
{
public:
    explicit MCGamesList(MCGamesWhere where);
    ~MCGamesList();
    MCGamesList(const MCGamesList&) = delete;
    MCGamesList& operator=(const MCGamesList&) = delete;

    MCGamesWhere Where() const { return _Where; }

    /// <summary>Starts listening; the list starts again empty.</summary>
    void Open();
    /// <summary>Stops listening; the games stay as last heard.</summary>
    void Close();
    bool IsOpen() const { return _Open; }

    /// <summary>
    /// RETRY: the service's list asked again at once (its socket reopened), or this network's games asked for again.
    /// </summary>
    void Retry();

    /// <summary>Moves it along (the service's messages, this network's answers); whether what it shows changed.</summary>
    bool Update();

    MCGamesState State() const;
    /// <summary>The games, open ones first; none while <see cref="State"/> isn't Listed.</summary>
    std::span<const MCListedGame> Games() const { return _Games; }
    /// <summary>The game with <paramref name="key"/>, while listed.</summary>
    std::optional<MCListedGame> Find(std::string_view key) const;

    /// <summary>The service's counts, once it has sent them: every copy online, and those in games.</summary>
    std::optional<std::pair<int32_t, int32_t>> Counts() const;
    /// <summary>When most players are on, in this machine's time ("Fri to Sun, 7 to 11 PM"); empty before it's heard.</summary>
    std::string Busiest() const;

private:
    /// <summary>This network's games read again from the session manager (each with its players).</summary>
    bool ReadLanGames();
    /// <summary>The service's games as the list shows them.</summary>
    bool ReadInternetGames();

    MCGamesWhere _Where;
    bool _Open = false;
    std::vector<MCListedGame> _Games;
    std::unique_ptr<MCInternetList> _List;
    /// <summary>LAN: whether this network has been asked and its answers read once (Looking until then).</summary>
    bool _LanListed = false;
    /// <summary>LAN: the next time this network's games are read (its answers come in between).</summary>
    std::chrono::steady_clock::time_point _NextLanRead;
};

/// <summary>
/// A game being joined or hosted (JOIN GAME, a code, HOST GAME's window): the session manager's internet game or this
/// network's session, moved along a frame at a time (<see cref="Tend"/>) while the screen stays live; ends joined (the
/// room set up), refused (<see cref="Refusal"/> says why, in the screens' words) or called off.
/// </summary>
class MCGameJoin
{
public:
    /// <summary>What the player asked for.</summary>
    struct MCAsk
    {
        MCGamesWhere Where = MCGamesWhere::Internet;
        bool Hosting = false;
        /// <summary>The game as the windows name it: its name, or "the game K7Q2" for a code typed.</summary>
        std::string Shown;
        /// <summary>Joining: the internet game's code, or the LAN session's key.</summary>
        std::string Key;
        /// <summary>A code typed, not a game picked from the list.</summary>
        bool Typed = false;
        std::string PlayerName;
        /// <summary>Hosting: its name, how many players, and who can join (PUBLIC; a LAN game is this network's).</summary>
        std::string GameName;
        int32_t MaxPlayers = 6;
        bool Public = true;
    };

    /// <summary>How it ended.</summary>
    enum class MCEnd
    {
        /// <summary>Still under way.</summary>
        None,
        /// <summary>In the game: its room set up by <see cref="Tend"/>'s caller.</summary>
        Joined,
        /// <summary>Refused, or nothing answered: <see cref="Refusal"/> says why.</summary>
        Refused,
        /// <summary>Called off (CANCEL).</summary>
        CalledOff
    };

    /// <summary>
    /// Starts joining or hosting as <paramref name="ask"/> says; false (with <see cref="Refusal"/> set) when it can't
    /// start (no multiplayer game, a build without internet games).
    /// </summary>
    bool Begin(MCAsk ask);

    /// <summary>Moves it along: the service's answer, the link, then the room joined or made; its end.</summary>
    MCEnd Tend();

    /// <summary>CANCEL: the game left (its internet game ended), nothing said.</summary>
    void CallOff();

    /// <summary>Whether one is under way.</summary>
    bool Pending() const { return _Ask.has_value() && _End == MCEnd::None; }
    MCEnd End() const { return _End; }
    /// <summary>What was asked (while under way, and after its end).</summary>
    const std::optional<MCAsk>& Asked() const { return _Ask; }

    /// <summary>Why it was refused, one sentence naming the game ("Steel Rain is full."); empty otherwise.</summary>
    const std::string& Refusal() const { return _Refusal; }
    /// <summary>Whether trying again can help (RETRY under the refusal).</summary>
    bool CanRetry() const { return _CanRetry; }
    /// <summary>The service said this game must update to play online (UPDATE under the refusal).</summary>
    bool NeedsUpdate() const { return _NeedsUpdate; }

    /// <summary>The screens' words.</summary>
    static constexpr std::string_view JoiningFormat = "Joining {}...";
    static constexpr std::string_view HostingText = "Setting up your game...";
    static constexpr std::string_view CodeGameFormat = "the game {}";

private:
    void Refuse(std::string text, bool canRetry = false, bool needsUpdate = false);
    /// <summary>The service's refusal (or nothing answering) in the screens' words.</summary>
    void RefuseFor(std::optional<MCServiceRefusal> refusal);
    MCEnd TendInternet();
    MCEnd TendLan();

    std::optional<MCAsk> _Ask;
    MCEnd _End = MCEnd::None;
    std::string _Refusal;
    bool _CanRetry = false;
    bool _NeedsUpdate = false;
    std::chrono::steady_clock::time_point _NextSearch;
    int32_t _Searches = 0;
};
