#pragma once

// Port: the service's part in an internet game's links (MCIceSignaling) over its WebSockets (platform/MCWebSocket), in
// the service's protocol (tools/cloudflare/games/README.md in the workspace). A host's socket to /host makes its game
// and gets the game's code; a guest's to /join/<code> joins it. Each game's object on the service passes the links'
// signals, and the bytes of a link it carries (the WebSocket path), and makes the relays' credentials for each guest's
// links, the host's for that guest and the guest's own. A socket that drops comes back to its place with its token
// (/game/<code>), which the service keeps 5 minutes; the links it carried are gone, the others go on. The host says
// its room for the service's list. Every message from the service is checked before it's used.

#include "platform/MCIceTransport.h"
#include "platform/MCWebSocket.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

/// <summary>Who this game's sockets say it is: its copy's ID (network/MCInternetCopy) and its version.</summary>
struct MCServiceIdentity
{
    std::string Copy;
    std::string Version;
};

/// <summary>The host's room as the service's list shows it (a public one is listed).</summary>
struct MCServiceRoom
{
    std::string Name;
    bool Public = false;
    /// <summary>The most players, 2 to 6.</summary>
    int32_t Max = 0;
    std::vector<std::string> Players;
    /// <summary>The mission picked, or empty.</summary>
    std::string Mission;
    bool Battle = false;

    bool operator==(const MCServiceRoom&) const = default;
};

/// <summary>Why the service turned a socket away or ended its game: nothing comes back after one.</summary>
enum class MCServiceRefusal
{
    /// <summary>No game has that code.</summary>
    NoGame,
    /// <summary>The host's socket is away from the service: try again.</summary>
    HostAway,
    InBattle,
    Full,
    /// <summary>A dropped socket came back after its place was freed.</summary>
    PlaceGone,
    /// <summary>The game ended (its host left).</summary>
    GameOver,
    Banned,
    TooManyTries,
    /// <summary>The service speaks another protocol: this game must update.</summary>
    Update,
    /// <summary>Anything else the service refused.</summary>
    Refused,
    /// <summary>The service couldn't be reached, or a dropped socket couldn't come back while its place waited.</summary>
    Unreachable,
};

/// <summary>Whether <paramref name="text"/> is a game's code as the service makes them (four characters, no lookalikes).</summary>
bool MCIsGameCode(std::string_view text);

/// <summary>The refusal the service's message type <paramref name="type"/> names ("nogame", "update"...), if it names one.</summary>
std::optional<MCServiceRefusal> MCServiceRefusalNamed(std::string_view type);

/// <summary>The service's lengths for a game's name, a player's and a mission's (its protocol.js).</summary>
inline constexpr size_t MCServiceGameNameMost = 24;
inline constexpr size_t MCServicePlayerNameMost = 16;
inline constexpr size_t MCServiceMissionMost = 48;

/// <summary>
/// <paramref name="text"/> (the game's Windows-1252) as the service takes a name, which it refuses otherwise, closing
/// the host's socket: printable ASCII, an accented letter as its plain one ("é" e, "ß" ss), what has none left out,
/// runs of spaces as one, trimmed, cut to <paramref name="most"/>; <paramref name="fallback"/> when nothing is left.
/// </summary>
std::string MCServiceName(std::string_view text, size_t most, std::string_view fallback);

/// <summary>The service's protocol this game speaks (each socket's URL says it).</summary>
inline constexpr int MCServiceProtocol = 1;

/// <summary>The service's keep-alive, which it answers itself (a socket silent 5 minutes is closed).</summary>
inline constexpr std::string_view MCServicePing = "ping";
inline constexpr std::string_view MCServicePong = "pong";
inline constexpr std::chrono::seconds MCServicePingEvery{30};

/// <summary>How long a dropped socket's place waits (the service's 5 minutes), and the tries at coming back to it.</summary>
struct MCServiceComeback
{
    std::chrono::milliseconds PlaceKept = std::chrono::minutes(5);
    std::chrono::milliseconds FirstTry = std::chrono::seconds(1);
    std::chrono::milliseconds LongestWait = std::chrono::seconds(30);
};

/// <summary>The service's signals and a link's bytes, over its WebSockets. Thread-safe.</summary>
class MCServiceSignaling final : public MCIceSignaling
{
public:
    /// <summary>
    /// The service at <paramref name="service"/> (<c>wss://host</c>), its sockets opened by <paramref name="open"/>,
    /// saying this game is <paramref name="identity"/>. A public game's links take the relays' path only
    /// (MCIceLink::Settings::RelayOnly): a game it hosts is public with <paramref name="relayOnly"/>, a game it joins
    /// when the service says so (or doesn't say), and every link takes the relays only with
    /// <paramref name="relayOnly"/>.
    /// </summary>
    MCServiceSignaling(std::string service, MCWebSocketOpener open, MCServiceIdentity identity, bool relayOnly,
                       MCServiceComeback comeback = {});
    ~MCServiceSignaling() override;
    MCServiceSignaling(const MCServiceSignaling&) = delete;
    MCServiceSignaling& operator=(const MCServiceSignaling&) = delete;

    /// <summary>Hosts a game on the service: its code comes with the service's answer (<see cref="HostedCode"/>).</summary>
    void Host();
    /// <summary>The code of the game this one hosts, once the service gave it.</summary>
    std::optional<std::string> HostedCode();
    /// <summary>The hosted game's room as it stands now, for the service's list (sent when it changes).</summary>
    void SetRoom(const MCServiceRoom& room);
    /// <summary>Whether the service won't list the room by its name (the host picks another).</summary>
    bool NameRefused();
    /// <summary>Whether this game's socket to its host's game is in (the service sent the relays for its links).</summary>
    bool Joined(std::string_view game);
    /// <summary>Opens this game's socket to the game <paramref name="game"/> (the first signal to it would too).</summary>
    void Join(std::string_view game);
    /// <summary>Why the service ended the host's socket or <paramref name="game"/>'s, if it did.</summary>
    std::optional<MCServiceRefusal> Refusal(std::string_view game = {});
    /// <summary>Why the host's socket or <paramref name="game"/>'s closed, if it did (for the log and the trial).</summary>
    std::optional<std::string> WhyClosed(std::string_view game = {});
    /// <summary>
    /// Closes every socket, as this game's player leaving: the service frees its places, and a host's ends its game
    /// (as when it's destroyed). None comes back.
    /// </summary>
    void Leave();
    /// <summary>Whether every socket has closed (its leaving gone to the service, as far as the system can say).</summary>
    bool AllClosed();
    /// <summary>
    /// The links bind to <paramref name="address"/> alone (MCIceLink::Settings::BindAddress): the tests' loopback, which
    /// the Mac's firewall never holds.
    /// </summary>
    void BindLinksTo(std::string address);

    MCIceLink::Settings LinkSettings(std::string_view peer) override;
    void ToHost(std::string_view game, uint32_t link, const MCIceLink::Signal& signal) override;
    void ToGuest(std::string_view guest, uint32_t link, const MCIceLink::Signal& signal) override;
    void BytesToHost(std::string_view game, uint32_t link, std::span<const uint8_t> bytes) override;
    void BytesToGuest(std::string_view guest, uint32_t link, std::span<const uint8_t> bytes) override;
    size_t Unsent() override;
    std::vector<Heard> Take() override;

private:
    using Clock = std::chrono::steady_clock;

    /// <summary>One socket: the host's to its game, or a guest's to a game it reached.</summary>
    struct Socket
    {
        std::unique_ptr<MCWebSocket> Ws;
        /// <summary>The service's hosted, joined or back came: links have their relays.</summary>
        bool In = false;
        /// <summary>This socket's closing was passed on (as the peer gone), so it's said once a socket.</summary>
        bool Told = false;
        Clock::time_point LastPing;
        /// <summary>The game's code and the place's token, from hosted or joined: what a dropped socket comes back with.</summary>
        std::string Code;
        std::string Token;
        /// <summary>A game reached: the relays its links start with (joined's).</summary>
        std::optional<MCIceLink::Settings> Settings;
        /// <summary>A game reached is public (joined's; one the service doesn't say is): its links take relays only.</summary>
        bool Public = true;
        /// <summary>Why the service ended it: no coming back.</summary>
        std::optional<MCServiceRefusal> Refusal;
        /// <summary>Since its socket dropped, while it tries to come back; the next try.</summary>
        std::optional<Clock::time_point> DroppedAt;
        Clock::time_point NextTry;
        int32_t Tries = 0;
    };

    Socket& GameSocket(std::string_view game);
    /// <summary>Closes every socket (under the lock).</summary>
    void CloseAll();
    /// <summary>A socket to the service's <paramref name="path"/>, or one closed at once when the system can't open it.</summary>
    std::unique_ptr<MCWebSocket> Opened(std::string_view path, std::string_view token = {});
    /// <summary>A closed socket's end told once, and its coming back tried while its place waits.</summary>
    void Tend(Socket& socket, std::string_view game, Clock::time_point now, std::vector<Heard>& heard);
    /// <summary>The peers a closed or away socket's links through the service went to: each heard as gone.</summary>
    void TellGone(std::string_view game, std::vector<Heard>& heard);
    void Ping(Socket& socket, Clock::time_point now);
    void SendRoom();
    /// <summary>A link's settings as this game takes them: relays only when <paramref name="relayOnly"/> or always.</summary>
    MCIceLink::Settings WithPolicy(MCIceLink::Settings settings, bool relayOnly) const;
    void HearHost(const MCWebSocket::Message& message, std::vector<Heard>& heard);
    void HearGame(std::string_view game, Socket& socket, const MCWebSocket::Message& message,
                  std::vector<Heard>& heard);

    std::string _Service;
    MCWebSocketOpener _Open;
    MCServiceIdentity _Identity;
    bool _RelayOnly = false;
    MCServiceComeback _Comeback;
    std::mutex _Lock;
    std::string _BindAddress;
    /// <summary>This game left (<see cref="Leave"/>): its sockets don't come back.</summary>
    bool _Left = false;
    std::optional<Socket> _Host;
    /// <summary>The hosted game's guests heard from (and not gone): told gone if the hosted game's socket closes.</summary>
    std::set<std::string, std::less<>> _Guests;
    /// <summary>The relays the service made for each guest's links, by the guest's number.</summary>
    std::map<uint32_t, MCIceLink::Settings> _GuestSettings;
    std::optional<MCServiceRoom> _Room;
    bool _NameRefused = false;
    std::map<std::string, Socket, std::less<>> _Games;
};
