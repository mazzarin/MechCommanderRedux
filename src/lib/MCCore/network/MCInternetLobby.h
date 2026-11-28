#pragma once

// Port (Redux HD): the INTERNET screens' view of the internet games' service (SPEC-in-game-lobby.md): the count online
// that MULTIPLAYER ACCESS asks once (GET /count), and the list the INTERNET screen keeps open (/list): the public games
// and the counts as they change, and the week's busiest hours. Each moves along across frames (Update), so the game's
// own thread never waits on the network. Every message from the service is checked before it's used.

#include "network/MCInternetGame.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/// <summary>
/// Port-only (Redux HD; SPEC-mco-hosting.md's "The switch"): MechCommander Online in INTERNET, as the service's list says
/// it to a screen as it opens: <see cref="Rooms"/> shows MCO's rooms in INTERNET, <see cref="Hosts"/> hosts INTERNET's
/// games on MCO too.
/// </summary>
struct MCMcoSwitchSaid
{
    bool Rooms = true;
    bool Hosts = true;

    bool operator==(const MCMcoSwitchSaid&) const = default;
};

/// <summary>The service's list of games and its counts, as the INTERNET screen shows them.</summary>
struct MCInternetListing
{
    /// <summary>
    /// The hex digits of a private game's key on the list (the service's game.js, LIST_KEY_DIGITS).
    /// </summary>
    static constexpr size_t PrivateKeyDigits = 16;

    /// <summary>A game, public or private: open ones come first, then those in battle.</summary>
    struct Game
    {
        /// <summary>A public game's code; empty for a private one (the list never carries it).</summary>
        std::string Code;
        /// <summary>
        /// Port-only (Redux HD; SPEC-in-game-lobby.md's "A private game on the list"): a private game, joined by the
        /// code its player types; listed by <see cref="Key"/>, which joins nothing.
        /// </summary>
        bool Private = false;
        /// <summary>A private game's key on the list (lowercase hex, <see cref="PrivateKeyDigits"/>); empty for a public one.</summary>
        std::string Key;
        std::string Name;
        /// <summary>Its players' names, the host first.</summary>
        std::vector<std::string> Players;
        /// <summary>The most players its host takes, 2 to 6.</summary>
        int32_t Max = 0;
        /// <summary>The mission its host picked, or empty.</summary>
        std::string Mission;
        /// <summary>The host's continent as Cloudflare names it (EU, NA...), or empty.</summary>
        std::string Region;
        /// <summary>The host's version as its build shows it (MC_VERSION_SHOWN).</summary>
        std::string Version;
        /// <summary>How long it has been in battle; none while it's open.</summary>
        std::optional<int64_t> BattleSeconds;

        bool operator==(const Game&) const = default;
    };

    /// <summary>
    /// Port-only (Redux HD; SPEC-mco-hosting.md's "On MCO"): a live game's MCO room as the service passes it on, its
    /// id and the game's name as its host sent it (MCServiceGameName, which its MCO room is named by too).
    /// </summary>
    struct McoRoom
    {
        std::string Id;
        std::string Name;

        bool operator==(const McoRoom&) const = default;
    };

    /// <summary>The most MCO rooms the service passes on (its list.js, MOST_MCO_ROOMS).</summary>
    static constexpr size_t MostMcoRooms = 100;

    /// <summary>Every copy connected to the service: on the INTERNET screen or in an internet game.</summary>
    int32_t Online = 0;
    /// <summary>Those of them in a game.</summary>
    int32_t InGames = 0;
    std::vector<Game> Games;
    /// <summary>Every live game's MCO room, public or not: INTERNET leaves MCO's own listing of each out.</summary>
    std::vector<McoRoom> McoRooms;

    bool operator==(const MCInternetListing&) const = default;
};

/// <summary>The hours in the service's week of busiest hours, from Sunday 00:00 UTC.</summary>
inline constexpr size_t MCWeekHours = 7 * 24;

/// <summary>
/// The week's hours from Sunday 00:00 UTC (<see cref="MCWeekHours"/> of them), each the mean of the online count's hourly
/// peaks over the service's last four weeks; none where it has none.
/// </summary>
using MCBusiestHours = std::vector<std::optional<double>>;

/// <summary>
/// When players are usually on, in the player's own time (<paramref name="utcOffset"/> from UTC), as the empty
/// INTERNET screen says it: "FRI TO SUN, 7 TO 11 PM" (the busiest days, then hours, each a run around its peak), a day
/// or an hour range alone when the other is every day or hour. Empty when <paramref name="hours"/> holds no peaks.
/// </summary>
std::string MCBusiestText(const MCBusiestHours& hours, std::chrono::minutes utcOffset);

/// <summary>This machine's offset from UTC now (its time zone and daylight saving).</summary>
std::chrono::minutes MCLocalUtcOffset();

/// <summary>
/// The service's list, kept open while the INTERNET screen is: a socket to /list that pings every 30 s and comes back
/// after a drop (the service's deploy drops it), its listing gone until it's back.
/// </summary>
class MCInternetList
{
public:
    /// <summary>Opens the list; null when internet games aren't available (no socket opener set).</summary>
    static std::unique_ptr<MCInternetList> Open();

    ~MCInternetList();
    MCInternetList(const MCInternetList&) = delete;
    MCInternetList& operator=(const MCInternetList&) = delete;

    /// <summary>Moves it along (the service's messages, its pings, coming back after a drop).</summary>
    /// <returns>Whether the listing or the busiest hours changed.</returns>
    bool Update();
    /// <summary>The list as the service last sent it; none until it has, and while its socket is away.</summary>
    const std::optional<MCInternetListing>& Listing() const { return _Listing; }
    /// <summary>The week's busiest hours, as the service sent them with its first list (empty before).</summary>
    const MCBusiestHours& Busiest() const { return _Busiest; }
    /// <summary>
    /// Port-only (Redux HD): MCO's switch, as the service last said it (with its first list to each socket); none until
    /// it has.
    /// </summary>
    const std::optional<MCMcoSwitchSaid>& McoSwitch() const { return _McoSwitch; }
    /// <summary>Why the service turned it away for good (another protocol, a ban): none while it lives.</summary>
    std::optional<MCServiceRefusal> Refusal() const { return _Refusal; }
    /// <summary>
    /// Port-only (SPEC-menus.md's stage 3): why its socket last closed before the service sent a list (the socket's
    /// MCWebSocket::WhyClosed), while it tries again; none once a list is in, or before the first try ends.
    /// </summary>
    const std::optional<std::string>& Trouble() const { return _Trouble; }

private:
    using Clock = std::chrono::steady_clock;

    MCInternetList(MCWebSocketOpener open, std::string url);
    void Reopen(Clock::time_point now);
    /// <summary>Reads one message from the service: true when it changed what the screen shows.</summary>
    bool Hear(const MCWebSocket::Message& message);
    /// <summary>The socket's gone: the listing with it, and the next try at coming back.</summary>
    void Dropped(Clock::time_point now, std::chrono::milliseconds wait);

    MCWebSocketOpener _Open;
    std::string _Url;
    std::unique_ptr<MCWebSocket> _Ws;
    std::optional<MCInternetListing> _Listing;
    MCBusiestHours _Busiest;
    std::optional<MCMcoSwitchSaid> _McoSwitch;
    std::optional<MCServiceRefusal> _Refusal;
    std::optional<std::string> _Trouble;
    Clock::time_point _LastPing;
    /// <summary>When the socket comes back after a drop (none while it's up), and the tries at it since it was last in.</summary>
    std::optional<Clock::time_point> _NextTry;
    int32_t _Tries = 0;
};

/// <summary>
/// The count online, asked once (MULTIPLAYER ACCESS): GET /count on a thread of its own, the answer read on the game's.
/// Destroying it stops the transfer under way and returns at once: the thread, which owns what it uses, ends on its own
/// and the thread keeper (lib/MCThreadKeeper) joins it.
/// </summary>
class MCInternetCount
{
public:
    /// <summary>Asks the service; null when internet games aren't available (no transfer set).</summary>
    static std::unique_ptr<MCInternetCount> Ask();

    ~MCInternetCount();
    MCInternetCount(const MCInternetCount&) = delete;
    MCInternetCount& operator=(const MCInternetCount&) = delete;

    /// <summary>Whether the service answered, or the asking failed (the log says why).</summary>
    bool Done();
    /// <summary>The count once the service answered it; none while waiting, or when it couldn't be had.</summary>
    std::optional<int32_t> Online();

private:
    /// <summary>What the asking thread and the count share: the transfer, and the answer once it's in.</summary>
    struct MCAsking;

    MCInternetCount(std::unique_ptr<MCHttpSource::MCGet> get, std::string url);

    std::shared_ptr<MCAsking> _Asking;
    /// <summary>Last, so it starts once what it shares is made.</summary>
    std::thread _Thread;
};
