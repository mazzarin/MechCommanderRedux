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

    /// <summary>The Redux engine's protocol a room plays (the relay's, mco-redux-stock/2).</summary>
    inline constexpr int32_t EngineProtocol = 2;
    /// <summary>The most players a room holds, and the fewest a match starts with (MCO's launcher's rule).</summary>
    inline constexpr int32_t RoomMost = 6;
    inline constexpr int32_t MatchFewest = 2;
    /// <summary>The lowest port a relay listens on, as the launcher holds a match to.</summary>
    inline constexpr int32_t RelayPortLeast = 1024;

    /// <summary>A Redux match, as the host's create_session or a guest's start_signal gives it.</summary>
    struct MCMatch
    {
        /// <summary>The match's id (32 hex), its session's.</summary>
        std::string SessionHex;
        std::string EngineBuild;
        std::string EngineContent;
        std::string RoomId;
        /// <summary>The relay the match is played through.</summary>
        std::string RelayHost;
        uint16_t RelayPort = 0;
        int32_t Capacity = 0;
    };

    /// <summary>The match's ticket to the relay (redux_ticket's admission), checked against the match and the player.</summary>
    struct MCAdmission
    {
        std::string Ticket;
        /// <summary>"host" or "join".</summary>
        std::string Role;
    };

    /// <summary>A player in a room.</summary>
    struct MCRoomPlayer
    {
        std::string ClientId;
        std::string Username;
        bool Ready = false;
        bool Host = false;
    };

    /// <summary>A line of a room's chat, as room_poll's messages give it (MCO's launcher's reading).</summary>
    struct MCRoomMessage
    {
        int64_t Id = 0;
        /// <summary>Who said it: none for MCO's own lines (<see cref="System"/>).</summary>
        std::string SenderId;
        std::string Username;
        /// <summary>What was said (MCO's own line's text when it names no event).</summary>
        std::string Text;
        /// <summary>MCO's own line: a player joining, leaving or taken out, the host handing on, the game started.</summary>
        bool System = false;
        /// <summary>MCO's own line's event (room_event: "joined", "left", "removed", ...) and its kind (event_kind).</summary>
        std::string Event;
        std::string EventKind;
        /// <summary>Whom MCO's own line is about.</summary>
        std::string EventPlayer;
        /// <summary>The message's kind (type): a picture's, a GIF's or a voice message's carry no text.</summary>
        std::string Kind;
        /// <summary>Its sender's own id for it (send_message's request_id), which the sender's copy is matched by.</summary>
        std::string RequestId;
        bool operator==(const MCRoomMessage&) const = default;
    };

    /// <summary>Where a player sits: the room it registered in and how far its polls have read the room's messages.</summary>
    struct MCSeat
    {
        std::string Room;
        /// <summary>The room's id (32 hex), once known (a joined room's; a hosted one's from its first poll).</summary>
        std::string RoomId;
        bool Host = false;
        bool Ready = false;
        int64_t LastMessageId = 0;
    };

    /// <summary>A room as one poll found it.</summary>
    struct MCRoomState
    {
        std::string Name;
        std::vector<MCRoomPlayer> Players;
        /// <summary>The build and content the room plays (its settings').</summary>
        std::string EngineBuild;
        std::string EngineContent;
        /// <summary>The match the host started, when there is one (a guest's start_signal).</summary>
        std::optional<MCMatch> Start;
        /// <summary>The room's chat since the seat's cursor, oldest first.</summary>
        std::vector<MCRoomMessage> Messages;
        /// <summary>The player is no longer in the room, and why (MCO's exit_reason: its kind, and by whom).</summary>
        bool Gone = false;
        std::string GoneWhy;
        std::string GoneBy;
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

    /// <summary>
    /// <paramref name="username"/> as MCO's launcher hands it to the game (`-player`): its control characters (C0, DEL,
    /// C1) dropped, its first <see cref="MatchNameMost"/> characters; <see cref="MatchNameNone"/> when none are left.
    /// </summary>
    std::string MatchName(std::string_view username);
    inline constexpr size_t MatchNameMost = 40;
    inline constexpr std::string_view MatchNameNone = "MCO Player";

    /// <summary>The host's game's session for a match, as the launcher names it: "MCO " and its first 12 hex digits.</summary>
    std::string MatchSession(std::string_view sessionHex);

    /// <summary>A match's capacity as the host's game takes it (the launcher's: held to 2-6).</summary>
    int32_t MatchCapacity(int32_t capacity);

    /// <summary>Whether <paramref name="text"/> is <paramref name="digits"/> hex digits (either case).</summary>
    bool IsHex(std::string_view text, size_t digits);

    /// <summary>Whether two client ids are one (MCO compares them ignoring case).</summary>
    bool SameId(std::string_view one, std::string_view other);

    /// <summary>
    /// <paramref name="text"/> from MCO (a room's name, its error) as it may be shown at a terminal: each control
    /// character (C0, DEL, C1: an escape sequence), text-direction control and byte that isn't UTF-8 as '?'.
    /// </summary>
    std::string Printable(std::string_view text);

    /// <summary>
    /// <paramref name="error"/> as a line for the player: MCO's text, then its code, the HTTP status and how long to
    /// wait when there are ones (each <see cref="Printable"/>).
    /// </summary>
    std::string Describe(const MCError& error);

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

        /// <summary>
        /// The account a kept token is for, at a later start (session): its player, the token as given, held to the
        /// account <paramref name="clientId"/> it was kept for. Activate it after, as a sign-in.
        /// </summary>
        MCResult<MCSignIn> Resume(std::string_view accountToken, std::string_view clientId);

        /// <summary>
        /// SECURE PROFILE (secure): <paramref name="guest"/>'s profile (registered, its peer token) kept as an account
        /// with the email and password; the account as a sign-in's. Activate it after.
        /// </summary>
        MCResult<MCSignIn> Secure(const MCPeer& guest, std::string_view email, std::string_view password);

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

        /// <summary>
        /// Joins <paramref name="room"/> (register_peer into it, READY as asked), saying the game plays its build with
        /// <paramref name="content"/> (our game data's fingerprint, which must be the room's).
        /// </summary>
        MCResult<MCSeat> JoinRoom(MCPeer& peer, const MCRoom& room, std::string_view password, std::string_view content,
                                  bool ready);

        /// <summary>
        /// Hosts a room named <paramref name="name"/> for <paramref name="maxPlayers"/>, playing <paramref name="build"/>
        /// (the stock build) with <paramref name="content"/>.
        /// </summary>
        MCResult<MCSeat> HostRoom(MCPeer& peer, std::string_view name, int32_t maxPlayers, std::string_view password,
                                  std::string_view build, std::string_view content);

        /// <summary>
        /// The room as it is (room_poll), the seat's cursor carried on; while a match runs, its session and whether this
        /// player's game is in it (as MCO's launcher says, or the room drops the player).
        /// </summary>
        MCResult<MCRoomState> RoomPoll(MCPeer& peer, MCSeat& seat, std::string_view matchSession = {},
                                       bool matchRunning = false);

        /// <summary>
        /// Says <paramref name="text"/> in the room <paramref name="peer"/> is in (send_message), with
        /// <paramref name="requestId"/>, by which MCO's copy of it comes back.
        /// </summary>
        MCResult<void> SendMessage(MCPeer& peer, std::string_view text, std::string_view requestId);

        /// <summary>READY, or not (heartbeat).</summary>
        MCResult<void> SetReady(MCPeer& peer, MCSeat& seat, bool ready);

        /// <summary>The host's START (create_session): the match, for the room's build and content.</summary>
        MCResult<MCMatch> CreateSession(MCPeer& peer, std::string_view build, std::string_view content);

        /// <summary>The host's game is hosting through the relay (publish_ready).</summary>
        MCResult<void> PublishReady(MCPeer& peer, std::string_view session);

        /// <summary>
        /// The host's match is over (close_session): one whose game came back says the room's <paramref name="build"/>
        /// and <paramref name="content"/>, as the launcher does; one called off before, none.
        /// </summary>
        MCResult<void> CloseSession(MCPeer& peer, std::string_view session, std::string_view build = {},
                                    std::string_view content = {});

        /// <summary>A guest's match that's gone from the relay (report_session_missing).</summary>
        MCResult<void> ReportSessionMissing(MCPeer& peer, std::string_view session);

        /// <summary>
        /// The match's ticket to its relay (redux_ticket), for the relay's <paramref name="epoch"/>, held to the match,
        /// the player and its role as the launcher holds it.
        /// </summary>
        MCResult<MCAdmission> Ticket(MCPeer& peer, const MCMatch& match, std::string_view epoch, bool host);

        /// <summary>Leaves the room for the lobby (register_peer into the Lobby).</summary>
        MCResult<void> LeaveRoom(MCPeer& peer);

    private:
        struct MCAnswer;

        /// <summary>A registration (register_peer) with <paramref name="extra"/>, its token kept and its player checked.</summary>
        MCResult<MCAnswer> RegisterWith(MCPeer& peer, const MCFields& extra);

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
