#pragma once

#include "lib/MCOptionalRef.h"
#include "update/MCHttpTransfer.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// <summary>
/// Port-only (Redux HD): MechCommander Online's account and lobby services, spoken as its launcher speaks them
/// (SPEC-mco-rooms.md's "How it connects"), by a copy that names itself: its own device, user agent and system. The
/// account service takes JSON; the lobby's (its "chat" service) takes forms, each carrying the player's peer token,
/// which the service gives at the player's first registration and renews when it lapses (recover_peer). Every answer is
/// a JSON object whose "success" is true, or MCO's error: its text, its code and how long to wait. A password or token
/// goes only in a request's body, to the address asked (no redirect is followed), and nothing here logs one.
/// </summary>
namespace MCMcoApi
{
    /// <summary>MCO's site, and its two services' pages under it (the lobby's, "chat", and the account's).</summary>
    inline constexpr std::string_view Site = "https://mechcommanderonline.com/";
    inline constexpr std::string_view ChatPage = "chat_api.php";
    inline constexpr std::string_view AccountPage = "account_api.php";
    /// <summary>The launcher's release whose services these speak: the client_version sent (its update.json's).</summary>
    inline constexpr std::string_view LauncherRelease = "4.0.6.8";
    /// <summary>The lobby's room, where a player waits outside the game rooms.</summary>
    inline constexpr std::string_view LobbyRoom = "Lobby";
    /// <summary>The engine a Redux room names (a room without one is the original game's, "gold").</summary>
    inline constexpr std::string_view ReduxEngine = "redux";
    /// <summary>The most characters a player's name has (MCO's PlayerNamePolicy).</summary>
    inline constexpr size_t NameMost = 16;
    /// <summary>The most bytes an answer may have: past it, it isn't MCO's.</summary>
    inline constexpr size_t ReplyMost = size_t{4} << 20;
    /// <summary>The digits of a client id, and of an account's and a peer's token (hex).</summary>
    inline constexpr size_t ClientIdDigits = 32;
    inline constexpr size_t TokenDigits = 64;

    /// <summary>MCO's codes this code acts on.</summary>
    inline constexpr std::string_view ActiveDeviceCode = "active_device";
    inline constexpr std::string_view PeerExpiredCode = "peer_expired";
    inline constexpr std::string_view InvalidResponseCode = "invalid_response";

    /// <summary>What MCO refused, or why it couldn't be asked.</summary>
    struct MCError
    {
        /// <summary>MCO's own text, or what failed here.</summary>
        std::string Message;
        /// <summary>MCO's code ("invalid_credentials", <see cref="ActiveDeviceCode"/>), or ours, or none.</summary>
        std::string Code;
        /// <summary>The answer's HTTP status (0: no answer).</summary>
        int Status = 0;
        /// <summary>The seconds MCO asked to wait before asking again (0: none).</summary>
        int RetryAfter = 0;
        /// <summary>MCO couldn't be reached (no connection, its name not found, a timeout).</summary>
        bool Unreachable = false;
    };

    template <typename T> using MCResult = std::expected<T, MCError>;

    /// <summary>Who this copy is to MCO.</summary>
    struct MCClient
    {
        /// <summary>The device, 64 hex digits (<see cref="DeviceId"/>).</summary>
        std::string DeviceId;
        std::string UserAgent;
        /// <summary>The system, as register_peer's client_os (<see cref="ClientOs"/>).</summary>
        std::string ClientOs;
        std::string ClientVersion = std::string(LauncherRelease);
        /// <summary>The services' addresses: MCO's, or a stand-in's.</summary>
        std::string Chat = std::string(Site) + std::string(ChatPage);
        std::string Account = std::string(Site) + std::string(AccountPage);
    };

    /// <summary>A player on MCO: a guest's profile or an account's.</summary>
    struct MCPeer
    {
        /// <summary>The player's id, 32 hex digits: a guest's own (<see cref="NewClientId"/>), or its account's.</summary>
        std::string ClientId;
        /// <summary>The lobby's token for the player (none until its first registration, or its account's activation).</summary>
        std::string PeerToken;
        /// <summary>The account's session token (none: a guest).</summary>
        std::string AccountToken;
        std::string Username;
        /// <summary>
        /// The lobby's chat as far as the player's polls have had it (lobby_poll's cursors, as the launcher keeps
        /// them): the last message's id and the chat's revision, so a poll brings only what's new.
        /// </summary>
        int64_t LastMessageId = 0;
        int64_t MessageRevision = 0;
    };

    /// <summary>An account signed in (or made): its session token and its player.</summary>
    struct MCSignIn
    {
        std::string AccountToken;
        std::string ClientId;
        std::string Username;
        int64_t PlayerId = 0;
    };

    /// <summary>A room in MCO's lobby, as its launcher lists it.</summary>
    struct MCRoom
    {
        std::string RoomId;
        std::string Name;
        std::string Host;
        /// <summary>The game it plays: <see cref="ReduxEngine"/>, or "gold".</summary>
        std::string Engine;
        /// <summary>A Redux room's build ("official-0.2.1.61") and its content's fingerprint.</summary>
        std::string EngineBuild;
        std::string EngineContent;
        int32_t Players = 0;
        int32_t MaxPlayers = 0;
        bool InGame = false;
        bool Locked = false;
        bool AdmissionClosed = false;
    };

    /// <summary>The lobby as one poll found it.</summary>
    struct MCLobby
    {
        std::vector<MCRoom> Rooms;
        /// <summary>The players online (the lobby's and the rooms').</summary>
        size_t Players = 0;
    };

    /// <summary>A form's fields, in order.</summary>
    using MCFields = std::vector<std::pair<std::string, std::string>>;

    /// <summary>The fields as a form's body, as .NET's FormUrlEncodedContent writes them (a space as '+').</summary>
    std::string FormBody(const MCFields& fields);

    /// <summary>A guest's new client id: 128 random bits, 32 lower-case hex digits.</summary>
    std::string NewClientId();

    /// <summary>
    /// The device id for <paramref name="seed"/> (a copy's own random seed, kept), 64 hex digits: the SHA-256 of our
    /// prefix and the seed, so it's never MCO's launcher's for the same computer.
    /// </summary>
    std::string DeviceId(std::string_view seed);

    /// <summary>The system as client_os: its family ("macOS"), release ("26.5"), build, and the system's and app's CPU.</summary>
    std::string ClientOs(std::string_view family, std::string_view release, std::string_view build,
                         std::string_view systemCpu, std::string_view appCpu);

    /// <summary>
    /// <paramref name="name"/> as MCO takes a player's name (its PlayerNamePolicy): trimmed, valid UTF-8 with no control
    /// characters, at most <see cref="NameMost"/> characters; or what's wrong with it, to show the player.
    /// </summary>
    std::expected<std::string, std::string> PlayerName(std::string_view name);

    /// <summary>Whether <paramref name="text"/> is <paramref name="digits"/> hex digits (either case).</summary>
    bool IsHex(std::string_view text, size_t digits);

    /// <summary>Whether two client ids are one (MCO compares them ignoring case).</summary>
    bool SameId(std::string_view one, std::string_view other);

    /// <summary>
    /// <paramref name="text"/> from MCO (a room's name, its error) as it may be shown at a terminal: each control
    /// character (C0, DEL, C1: an escape sequence), text-direction control and byte that isn't UTF-8 as '?'.
    /// </summary>
    std::string Printable(std::string_view text);

    /// <summary>MCO's two services, asked through the platform's transfer (one request at a time, on this thread).</summary>
    class MCService
    {
    public:
        MCService(MCHttpSource::MCGet& transfer, MCClient client);

        /// <summary>Signs in to the account (login).</summary>
        MCResult<MCSignIn> Login(std::string_view email, std::string_view password);

        /// <summary>
        /// Makes this device the account's (activate), taking it from another computer when
        /// <paramref name="takeOver"/> (that one's waiting room session ends); else another computer's has
        /// <see cref="ActiveDeviceCode"/>.
        /// </summary>
        /// <returns>The account's peer token.</returns>
        MCResult<std::string> Activate(std::string_view accountToken, bool takeOver);

        /// <summary>Whether an account may be made from here (creation_status: MCO allows three a device or address).</summary>
        MCResult<bool> CanCreate();

        /// <summary>
        /// Makes an account of <paramref name="guest"/>'s profile (create): the guest registered (its peer token), its
        /// name, the account's email and password. Activate it after, as a sign-in.
        /// </summary>
        MCResult<MCSignIn> Create(const MCPeer& guest, std::string_view username, std::string_view email,
                                  std::string_view password);

        /// <summary>Ends the account's session (logout): its token is no longer good.</summary>
        MCResult<void> Logout(std::string_view accountToken);

        /// <summary>
        /// Registers <paramref name="peer"/> in the lobby (register_peer), keeping the peer token the service gives
        /// (a guest's first).
        /// </summary>
        MCResult<void> Register(MCPeer& peer);

        /// <summary>The lobby's rooms and players (lobby_poll, else get_rooms where the poll has none).</summary>
        MCResult<MCLobby> Lobby(MCPeer& peer);

        /// <summary>The build MCO's Redux rooms play (redux_stock_status), or none when it names none.</summary>
        MCResult<std::optional<std::string>> StockBuild(MCPeer& peer);

        /// <summary>Takes <paramref name="peer"/> out of the lobby (unregister_peer).</summary>
        MCResult<void> Unregister(MCPeer& peer);

    private:
        struct MCAnswer;

        /// <summary>Asks the account service with <paramref name="body"/> (its JSON, its action in it).</summary>
        MCResult<MCAnswer> AskAccount(std::string body);

        /// <summary>
        /// Asks the lobby's service <paramref name="action"/> with <paramref name="extra"/>, as <paramref name="peer"/>
        /// (its identity fields first) when given; a lapsed peer token renewed once (recover_peer) and the question
        /// asked again.
        /// </summary>
        MCResult<MCAnswer> AskChat(std::string_view action, const MCFields& extra, MCOptionalRef<MCPeer> peer);

        MCResult<MCAnswer> AskChatOnce(std::string_view action, const MCFields& extra, MCOptionalRef<MCPeer> peer);

        /// <summary>Renews <paramref name="peer"/>'s lapsed token (recover_peer).</summary>
        MCResult<void> Recover(MCPeer& peer);

        MCResult<MCAnswer> Post(const std::string& address, std::string_view contentType, std::string& body);

        MCHttpSource::MCGet& _Transfer;
        MCClient _Client;
    };
}
