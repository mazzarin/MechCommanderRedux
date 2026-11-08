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

/// <summary>The service's list of public games and its counts, as the INTERNET screen shows them.</summary>
struct MCInternetListing
{
    /// <summary>A public game: open ones come first, then those in battle.</summary>
    struct Game
    {
        std::string Code;
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

    /// <summary>Every copy connected to the service: on the INTERNET screen or in an internet game.</summary>
    int32_t Online = 0;
    /// <summary>Those of them in a game.</summary>
    int32_t InGames = 0;
    std::vector<Game> Games;

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
    /// <summary>Why the service turned it away for good (another protocol, a ban): none while it lives.</summary>
    std::optional<MCServiceRefusal> Refusal() const { return _Refusal; }

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
    std::optional<MCServiceRefusal> _Refusal;
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
