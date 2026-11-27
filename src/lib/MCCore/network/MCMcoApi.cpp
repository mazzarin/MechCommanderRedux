#include "stdafx.h"
#include "network/MCMcoApi.h"

#include "lib/MCRandomId.h"
#include "lib/MCSha256.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <limits>

namespace
{
    using Json = nlohmann::json;

    constexpr std::string_view JsonType = "application/json";
    constexpr std::string_view FormType = "application/x-www-form-urlencoded";
    /// <summary>Our device ids' prefix: MCO's launcher hashes "MechCommanderOnline.Device.v1:" and its seed.</summary>
    constexpr std::string_view DevicePrefix = "MechCommanderReduxHD.Device.v1:";

    /// <summary>What every lobby request says of the Redux protocols it speaks (the launcher's 4.0.6.8).</summary>
    constexpr std::string_view ReduxProtocol = "1";
    constexpr std::string_view ReduxStockProtocol = "2";
    constexpr std::string_view RoomEventVersion = "2";
    /// <summary>The players a lobby registration's room holds, as the launcher registers its lobby.</summary>
    constexpr std::string_view LobbyPlayers = "6";
    constexpr std::string_view OnlineStatus = "Online";
    constexpr std::string_view Zero = "0";
    constexpr int32_t DefaultMaxPlayers = 6;
    constexpr std::string_view GoldEngine = "gold";
    /// <summary>An account's name when its profile names none, as the launcher names it.</summary>
    constexpr std::string_view DefaultUsername = "Player";

    constexpr std::string_view ReduxEngineProtocol = "2";
    constexpr std::string_view HostFlag = "1";
    constexpr std::string_view ReadyFlag = "1";
    /// <summary>The room's messages a poll asks for at once, as the launcher asks.</summary>
    constexpr std::string_view RoomMessagesAsked = "50";
    /// <summary>A match's role on the relay.</summary>
    constexpr std::string_view HostRole = "host";
    constexpr std::string_view JoinRole = "join";
    /// <summary>A ticket's length and characters the launcher takes (letters, digits, '-', '.', '_').</summary>
    constexpr size_t TicketFewest = 64;
    constexpr size_t TicketMost = 3500;
    constexpr int32_t PortMost = 65535;
    constexpr std::string_view NoMatchCode = "invalid_match";

    constexpr std::string_view RegisterAction = "register_peer";
    constexpr std::string_view RecoverAction = "recover_peer";
    constexpr std::string_view UnregisterAction = "unregister_peer";
    constexpr std::string_view ResetAction = "reset_profile";
    /// <summary>The lobby requests that carry no identity_ids (the launcher's PostCoreAsync).</summary>
    constexpr std::array<std::string_view, 4> NoIdentityActions = {RegisterAction, RecoverAction, UnregisterAction,
                                                                   ResetAction};
    /// <summary>The lobby requests that say the system (client_os).</summary>
    constexpr std::array<std::string_view, 2> SystemActions = {RegisterAction, RecoverAction};

    /// <summary>MCO's text for a lapsed peer token that comes without its code (older services).</summary>
    constexpr std::string_view PeerExpiredText = "Peer session expired; rejoin the lobby.";
    constexpr std::string_view RefusedText = "MechCommander Online refused the request.";
    constexpr std::string_view TooLongCode = "response_too_large";
    constexpr std::string_view UnreachableCode = "connection_failed";
    constexpr std::string_view IdentityChangedCode = "identity_changed";

    constexpr int HttpOkFirst = 200;
    constexpr int HttpOkLast = 299;
    /// <summary>
    /// What isn't shown of MCO's text (Printable): C0's controls, DEL and C1's (U+007F to U+009F), the text-direction
    /// embeddings and overrides (U+202A to U+202E), isolates (U+2066 to U+2069) and marks.
    /// </summary>
    constexpr Uint32 ControlsEnd = 0x20;
    constexpr Uint32 DeleteFirst = 0x7f;
    constexpr Uint32 C1End = 0xa0;
    constexpr Uint32 DirectionFirst = 0x202a;
    constexpr Uint32 DirectionLast = 0x202e;
    constexpr Uint32 IsolateFirst = 0x2066;
    constexpr Uint32 IsolateLast = 0x2069;
    constexpr Uint32 LeftToRightMark = 0x200e;
    constexpr Uint32 RightToLeftMark = 0x200f;
    /// <summary>An account email's length MCO allows (its CredentialFromResponse).</summary>
    constexpr size_t EmailFewest = 3;
    constexpr size_t EmailMost = 254;

    /// <summary>Gathers an answer, up to <see cref="MCMcoApi::ReplyMost"/> bytes.</summary>
    class MCGather final : public MCHttpSource::MCReply
    {
    public:
        bool Status(int status, std::string_view /*contentRange*/) override
        {
            Code = status;
            return true;
        }

        bool Body(std::span<const char> bytes) override
        {
            if (Text.size() + bytes.size() > MCMcoApi::ReplyMost)
            {
                TooLong = true;
                return false;
            }

            Text.append(bytes.data(), bytes.size());
            return true;
        }

        int Code = 0;
        std::string Text;
        bool TooLong = false;
    };

    /// <summary>A string member of <paramref name="object"/>, or <paramref name="otherwise"/> (missing, or not a string).</summary>
    std::string TextOf(const Json& object, std::string_view key, std::string_view otherwise = {})
    {
        if (!object.is_object())
        {
            return std::string(otherwise);
        }

        const auto found = object.find(key);
        return found != object.end() && found->is_string() ? found->get<std::string>() : std::string(otherwise);
    }

    /// <summary>A whole-number member of <paramref name="object"/>, or <paramref name="otherwise"/>.</summary>
    int64_t NumberOf(const Json& object, std::string_view key, int64_t otherwise)
    {
        if (!object.is_object())
        {
            return otherwise;
        }

        const auto found = object.find(key);

        if (found == object.end())
        {
            return otherwise;
        }

        if (found->is_number_integer())
        {
            return found->get<int64_t>();
        }

        // MCO's PHP sends some counts as text.
        if (found->is_string())
        {
            const std::string text = found->get<std::string>();
            int64_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            return error == std::errc() && end == text.data() + text.size() ? value : otherwise;
        }

        return otherwise;
    }

    /// <summary>A flag member of <paramref name="object"/>: true, 1, "1" or "true" (MCO's PHP sends each).</summary>
    bool FlagOf(const Json& object, std::string_view key)
    {
        if (!object.is_object())
        {
            return false;
        }

        const auto found = object.find(key);

        if (found == object.end())
        {
            return false;
        }

        if (found->is_boolean())
        {
            return found->get<bool>();
        }

        if (found->is_number_integer())
        {
            return found->get<int64_t>() != 0;
        }

        return found->is_string() && (*found == "1" || *found == "true");
    }

    /// <summary>A count read from MCO as a player count: none below 0, none past what an int32 holds.</summary>
    int32_t CountOf(int64_t value)
    {
        return static_cast<int32_t>(std::clamp<int64_t>(value, 0, std::numeric_limits<int32_t>::max()));
    }

    /// <summary>An answer that isn't MCO's: not JSON, not an object.</summary>
    MCMcoApi::MCError NotMcos(int status, std::string_view what)
    {
        return {.Message = std::format("MechCommander Online's answer (HTTP {}) {}.", status, what),
                .Code = std::string(MCMcoApi::InvalidResponseCode),
                .Status = status};
    }

    /// <summary>Overwrites <paramref name="text"/> (a body that held a password or token) before it's freed.</summary>
    void Wipe(std::string& text)
    {
        std::ranges::fill(text, '\0');
        text.clear();
    }

    /// <summary>The account in a login's or create's answer, held to what MCO's launcher accepts.</summary>
    MCMcoApi::MCResult<MCMcoApi::MCSignIn> SignInFrom(const Json& answer)
    {
        const auto account = answer.find("account");

        if (account == answer.end() || !account->is_object())
        {
            return std::unexpected(MCMcoApi::MCError{.Message = "The account service sent no account.",
                                                     .Code = std::string(MCMcoApi::InvalidResponseCode)});
        }

        MCMcoApi::MCSignIn signIn{.AccountToken = TextOf(answer, "account_token"),
                                  .ClientId = TextOf(*account, "client_id"),
                                  .Username = std::string(DefaultUsername)};
        const size_t emailLength = TextOf(*account, "email").size();
        const std::string peerToken = TextOf(answer, "peer_token");

        if (!MCMcoApi::IsHex(signIn.AccountToken, MCMcoApi::TokenDigits) ||
            !MCMcoApi::IsHex(signIn.ClientId, MCMcoApi::ClientIdDigits) || emailLength < EmailFewest ||
            emailLength > EmailMost || NumberOf(answer, "expires_at", 0) < 1 ||
            (!peerToken.empty() && !MCMcoApi::IsHex(peerToken, MCMcoApi::TokenDigits)))
        {
            return std::unexpected(MCMcoApi::MCError{.Message = "The account service sent an account it can't have.",
                                                     .Code = std::string(MCMcoApi::InvalidResponseCode)});
        }

        if (const auto profile = answer.find("profile"); profile != answer.end() && profile->is_object())
        {
            signIn.Username = TextOf(*profile, "username", DefaultUsername);
            signIn.PlayerId = NumberOf(*profile, "player_id", 0);
        }

        return signIn;
    }

    /// <summary>A room of a lobby's "rooms".</summary>
    MCMcoApi::MCRoom RoomFrom(const Json& room)
    {
        const auto players = room.find("players");
        const int64_t listed = players != room.end() && players->is_array() ? static_cast<int64_t>(players->size()) : 0;
        return {.RoomId = TextOf(room, "room_id"),
                .Name = TextOf(room, "name"),
                .Host = TextOf(room, "host"),
                .Engine = TextOf(room, "engine", GoldEngine),
                .EngineBuild = TextOf(room, "engine_build"),
                .EngineContent = TextOf(room, "engine_content"),
                .Players = CountOf(NumberOf(room, "player_count", listed)),
                .MaxPlayers = CountOf(NumberOf(room, "max_players", DefaultMaxPlayers)),
                .InGame = FlagOf(room, "in_game"),
                .Locked = FlagOf(room, "locked"),
                .AdmissionClosed = FlagOf(room, "admission_closed")};
    }

    /// <summary>The rooms an answer lists, or none when it has no "rooms" array.</summary>
    std::optional<std::vector<MCMcoApi::MCRoom>> RoomsFrom(const Json& answer)
    {
        const auto rooms = answer.find("rooms");

        if (rooms == answer.end() || !rooms->is_array())
        {
            return std::nullopt;
        }

        std::vector<MCMcoApi::MCRoom> found;

        for (const Json& room : *rooms)
        {
            if (room.is_object())
            {
                found.push_back(RoomFrom(room));
            }
        }

        return found;
    }

    /// <summary>
    /// A match as the host's create_session or a guest's start_signal gives it, held to what the launcher takes (its
    /// StartAsync): the Redux engine's protocol, 32-hex session and room, a relay named by DNS on a port it allows.
    /// </summary>
    MCMcoApi::MCResult<MCMcoApi::MCMatch> MatchFrom(const Json& session)
    {
        MCMcoApi::MCMatch match{.SessionHex = TextOf(session, "session_hex"),
                                .EngineBuild = TextOf(session, "engine_build"),
                                .EngineContent = TextOf(session, "engine_content"),
                                .RoomId = TextOf(session, "room_id"),
                                .RelayHost = TextOf(session, "stock_host"),
                                .Capacity = CountOf(NumberOf(session, "capacity", 0))};
        const int64_t port = NumberOf(session, "stock_port", 0);
        const auto refused = [](std::string_view why)
        { return std::unexpected(MCMcoApi::MCError{.Message = std::string(why), .Code = std::string(NoMatchCode)}); };

        if (NumberOf(session, "engine_protocol", 0) != MCMcoApi::EngineProtocol)
        {
            return refused("The match plays another Redux protocol.");
        }

        if (!MCMcoApi::IsHex(match.SessionHex, MCMcoApi::ClientIdDigits) ||
            !MCMcoApi::IsHex(match.RoomId, MCMcoApi::ClientIdDigits))
        {
            return refused("The match has no session.");
        }

        // A name (as .NET's Uri.CheckHostName calls one DNS's): its labels letters, digits and hyphens, none empty, more
        // than one, and not an address's four numbers.
        const std::string_view host = match.RelayHost;
        const bool labelsWhole =
            !host.empty() && !host.starts_with('.') && !host.ends_with('.') && !host.contains("..");
        const bool named =
            labelsWhole && host.contains('.') &&
            std::ranges::all_of(host, [](char c)
                                { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-'; }) &&
            !std::ranges::all_of(host,
                                 [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.'; });

        if (!named || port < MCMcoApi::RelayPortLeast || port > PortMost)
        {
            return refused("The match's relay isn't one.");
        }

        match.RelayPort = static_cast<uint16_t>(port);
        return match;
    }

    /// <summary>A room's players, as its poll lists them.</summary>
    std::vector<MCMcoApi::MCRoomPlayer> PlayersFrom(const Json& answer)
    {
        std::vector<MCMcoApi::MCRoomPlayer> players;
        const auto peers = answer.find("peers");

        if (peers == answer.end() || !peers->is_array())
        {
            return players;
        }

        for (const Json& peer : *peers)
        {
            if (peer.is_object())
            {
                players.push_back({.ClientId = TextOf(peer, "client_id"),
                                   .Username = TextOf(peer, "username"),
                                   .Ready = FlagOf(peer, "is_ready"),
                                   .Host = FlagOf(peer, "is_host")});
            }
        }

        return players;
    }

    /// <summary>The newest message id a room's answer has had: its own, or its messages' highest.</summary>
    int64_t RoomCursorFrom(const Json& answer, int64_t cursor)
    {
        cursor = std::max(cursor, NumberOf(answer, "last_id", cursor));
        cursor = std::max(cursor, NumberOf(answer, "room_last_message_id", cursor));

        if (const auto messages = answer.find("messages"); messages != answer.end() && messages->is_array())
        {
            for (const Json& message : *messages)
            {
                cursor = std::max(cursor, NumberOf(message, "id", cursor));
            }
        }

        return cursor;
    }

    /// <summary>
    /// Appends <paramref name="text"/> to a form's body as .NET's FormUrlEncodedContent does: letters, digits and
    /// "-_.~" as they are, a space as '+', every other byte as %XX.
    /// </summary>
    void AppendFormText(std::string& body, std::string_view text)
    {
        constexpr std::string_view Unreserved = "-_.~";

        for (const char c : text)
        {
            const bool letterOrDigit = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');

            if (letterOrDigit || Unreserved.contains(c))
            {
                body += c;
            }
            else if (c == ' ')
            {
                body += '+';
            }
            else
            {
                body += std::format("%{:02X}", static_cast<unsigned char>(c));
            }
        }
    }

    /// <summary>An account request's body: <paramref name="fields"/> with its action and the device.</summary>
    std::string AccountBody(std::string_view action, std::string_view deviceId, Json fields)
    {
        fields["action"] = action;
        fields["device_id"] = deviceId;
        return fields.dump();
    }

    /// <summary>
    /// Moves <paramref name="peer"/>'s chat cursors on past a lobby poll's <paramref name="answer"/>, as the launcher
    /// does: a new revision of the chat starts its messages over; the last id is the highest the answer names.
    /// </summary>
    void FollowChat(const Json& answer, MCMcoApi::MCPeer& peer)
    {
        if (const auto revision = answer.find("message_revision"); revision != answer.end())
        {
            const int64_t now = NumberOf(answer, "message_revision", peer.MessageRevision);

            if (peer.MessageRevision != 0 && now != peer.MessageRevision)
            {
                peer.LastMessageId = 0;
            }

            peer.MessageRevision = now;
        }

        peer.LastMessageId = std::max(peer.LastMessageId, NumberOf(answer, "last_id", 0));

        if (const auto messages = answer.find("messages"); messages != answer.end() && messages->is_array())
        {
            for (const Json& message : *messages)
            {
                peer.LastMessageId = std::max(peer.LastMessageId, NumberOf(message, "id", 0));
            }
        }
    }

    /// <summary>Whether <paramref name="action"/> is one of <paramref name="actions"/>.</summary>
    template <size_t Count> bool Among(std::string_view action, const std::array<std::string_view, Count>& actions)
    {
        return std::ranges::find(actions, action) != actions.end();
    }
}

namespace MCMcoApi
{
    /// <summary>An answer MCO gave with success: its JSON object.</summary>
    struct MCService::MCAnswer
    {
        Json Object;
    };

    std::string FormBody(const MCFields& fields)
    {
        std::string body;

        for (const auto& [name, value] : fields)
        {
            if (!body.empty())
            {
                body += '&';
            }

            AppendFormText(body, name);
            body += '=';
            AppendFormText(body, value);
        }

        return body;
    }

    std::string NewClientId()
    {
        return MCRandomId();
    }

    std::string DeviceId(std::string_view seed)
    {
        return MCSha256::Hex(MCSha256::Of(std::string(DevicePrefix) + std::string(seed)));
    }

    std::string ClientOs(std::string_view family, std::string_view release, std::string_view build,
                         std::string_view systemCpu, std::string_view appCpu)
    {
        return Json{
            {"family", family}, {"release", release}, {"build", build}, {"os_arch", systemCpu}, {"app_arch", appCpu}}
            .dump();
    }

    std::expected<std::string, std::string> PlayerName(std::string_view name)
    {
        constexpr std::string_view Spaces = " \t\r\n\v\f";
        const size_t first = name.find_first_not_of(Spaces);

        if (first == std::string_view::npos)
        {
            return std::unexpected("Enter a player name to continue.");
        }

        const std::string_view trimmed = name.substr(first, name.find_last_not_of(Spaces) - first + 1);
        const char* at = trimmed.data();
        size_t left = trimmed.size();
        size_t characters = 0;

        // MCO counts what's shown as one character (a letter and its marks); each code point here, so a name with
        // combining marks may be refused that MCO would take.
        while (left > 0)
        {
            const Uint32 point = SDL_StepUTF8(&at, &left);

            if (point == SDL_INVALID_UNICODE_CODEPOINT || point < ControlsEnd ||
                (point >= DeleteFirst && point < C1End))
            {
                return std::unexpected("Use only the characters allowed in player names.");
            }

            characters++;
        }

        if (characters > NameMost)
        {
            return std::unexpected(std::format("Player name must be {} characters or fewer.", NameMost));
        }

        return std::string(trimmed);
    }

    bool IsHex(std::string_view text, size_t digits)
    {
        return text.size() == digits &&
               std::ranges::all_of(text, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    }

    bool SameId(std::string_view one, std::string_view other)
    {
        return one.size() == other.size() &&
               std::ranges::equal(one, other,
                                  [](char a, char b)
                                  {
                                      return std::tolower(static_cast<unsigned char>(a)) ==
                                             std::tolower(static_cast<unsigned char>(b));
                                  });
    }

    std::string Printable(std::string_view text)
    {
        constexpr char Replaced = '?';
        std::string shown;
        const char* at = text.data();
        size_t left = text.size();

        while (left > 0)
        {
            const char* start = at;
            const Uint32 point = SDL_StepUTF8(&at, &left);
            const bool control = point < ControlsEnd || (point >= DeleteFirst && point < C1End);
            const bool direction = (point >= DirectionFirst && point <= DirectionLast) ||
                                   (point >= IsolateFirst && point <= IsolateLast) || point == LeftToRightMark ||
                                   point == RightToLeftMark;

            if (point == SDL_INVALID_UNICODE_CODEPOINT || control || direction)
            {
                shown += Replaced;
            }
            else
            {
                shown.append(start, static_cast<size_t>(at - start));
            }
        }

        return shown;
    }

    std::string Describe(const MCError& error)
    {
        std::string line = Printable(error.Message);

        if (!error.Code.empty())
        {
            line += std::format(" (code {})", Printable(error.Code));
        }

        if (error.Status != 0)
        {
            line += std::format(" (HTTP {})", error.Status);
        }

        if (error.RetryAfter != 0)
        {
            line += std::format(" (try again in {} s)", error.RetryAfter);
        }

        return line;
    }

    std::string MatchName(std::string_view username)
    {
        constexpr unsigned char Delete = 0x7f;
        constexpr unsigned char C1Lead = 0xc2;
        constexpr unsigned char C1Last = 0x9f;
        constexpr unsigned char ContinuationMask = 0xc0;
        constexpr unsigned char Continuation = 0x80;
        std::string name;
        size_t characters = 0;

        for (size_t i = 0; i < username.size() && characters <= MatchNameMost; i++)
        {
            const auto byte = static_cast<unsigned char>(username[i]);

            // A C1 control is U+0080-U+009F: 0xC2 then 0x80-0x9F in UTF-8.
            if (byte < ' ' || byte == Delete ||
                (byte == C1Lead && i + 1 < username.size() && static_cast<unsigned char>(username[i + 1]) <= C1Last))
            {
                i += byte == C1Lead ? 1 : 0;
                continue;
            }

            const bool starts = (byte & ContinuationMask) != Continuation;
            characters += starts ? 1 : 0;

            if (starts && characters > MatchNameMost)
            {
                break;
            }

            name += static_cast<char>(byte);
        }

        return name.empty() ? std::string(MatchNameNone) : name;
    }

    std::string MatchSession(std::string_view sessionHex)
    {
        constexpr std::string_view Prefix = "MCO ";
        constexpr size_t Digits = 12;
        return std::format("{}{}", Prefix, sessionHex.substr(0, Digits));
    }

    int32_t MatchCapacity(int32_t capacity)
    {
        return std::clamp(capacity, MatchFewest, RoomMost);
    }

    MCService::MCService(MCHttpSource::MCGet& transfer, MCClient client)
        : _Transfer(transfer), _Client(std::move(client))
    {
    }

    MCResult<MCSignIn> MCService::Login(std::string_view email, std::string_view password)
    {
        const auto answer =
            AskAccount(AccountBody("login", _Client.DeviceId, {{"email", email}, {"password", password}}));

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return SignInFrom(answer->Object);
    }

    MCResult<std::string> MCService::Activate(std::string_view accountToken, bool takeOver)
    {
        const auto answer = AskAccount(
            AccountBody("activate", _Client.DeviceId, {{"account_token", accountToken}, {"take_over", takeOver}}));

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        std::string peerToken = TextOf(answer->Object, "peer_token");

        if (!IsHex(peerToken, TokenDigits))
        {
            return std::unexpected(MCError{.Message = "The account service sent no peer token.",
                                           .Code = std::string(InvalidResponseCode)});
        }

        return peerToken;
    }

    MCResult<bool> MCService::CanCreate()
    {
        const auto answer = AskAccount(AccountBody("creation_status", _Client.DeviceId, Json::object()));

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        const auto available = answer->Object.find("available");

        if (available == answer->Object.end() || !available->is_boolean())
        {
            return std::unexpected(MCError{.Message = "The account service didn't say whether an account can be made.",
                                           .Code = std::string(InvalidResponseCode)});
        }

        return available->get<bool>();
    }

    MCResult<MCSignIn> MCService::Create(const MCPeer& guest, std::string_view username, std::string_view email,
                                         std::string_view password)
    {
        const auto answer = AskAccount(AccountBody("create", _Client.DeviceId,
                                                   {{"client_id", guest.ClientId},
                                                    {"peer_token", guest.PeerToken},
                                                    {"account_token", guest.AccountToken},
                                                    {"username", username},
                                                    {"email", email},
                                                    {"password", password}}));

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return SignInFrom(answer->Object);
    }

    MCResult<MCSignIn> MCService::Resume(std::string_view accountToken, std::string_view clientId)
    {
        const auto answer = AskAccount(AccountBody("session", _Client.DeviceId, {{"account_token", accountToken}}));

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        // As the launcher holds it: the account must be the one the token was kept for.
        const auto account = answer->Object.find("account");

        if (account == answer->Object.end() || !account->is_object() ||
            !SameId(TextOf(*account, "client_id"), clientId))
        {
            return std::unexpected(MCError{.Message = "The account service sent another account for the kept sign-in.",
                                           .Code = std::string(InvalidResponseCode)});
        }

        MCSignIn signIn{.AccountToken = std::string(accountToken),
                        .ClientId = TextOf(*account, "client_id"),
                        .Username = std::string(DefaultUsername)};

        if (const auto profile = answer->Object.find("profile");
            profile != answer->Object.end() && profile->is_object())
        {
            signIn.Username = TextOf(*profile, "username", DefaultUsername);
            signIn.PlayerId = NumberOf(*profile, "player_id", 0);
        }

        return signIn;
    }

    MCResult<MCSignIn> MCService::Secure(const MCPeer& guest, std::string_view email, std::string_view password)
    {
        const auto answer = AskAccount(AccountBody("secure", _Client.DeviceId,
                                                   {{"client_id", guest.ClientId},
                                                    {"peer_token", guest.PeerToken},
                                                    {"email", email},
                                                    {"password", password}}));

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return SignInFrom(answer->Object);
    }

    MCResult<void> MCService::Logout(std::string_view accountToken)
    {
        const auto answer = AskAccount(AccountBody("logout", _Client.DeviceId, {{"account_token", accountToken}}));

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return {};
    }

    MCResult<void> MCService::Register(MCPeer& peer)
    {
        const MCFields extra = {{"username", peer.Username},
                                {"room", std::string(LobbyRoom)},
                                {"is_host", std::string(Zero)},
                                {"max_players", std::string(LobbyPlayers)},
                                {"status", std::string(OnlineStatus)},
                                {"stream_url", ""},
                                {"away_message", ""}};
        const auto answer = RegisterWith(peer, extra);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return {};
    }

    MCResult<MCService::MCAnswer> MCService::RegisterWith(MCPeer& peer, const MCFields& extra)
    {
        auto answer = AskChat(RegisterAction, extra, peer);

        if (!answer)
        {
            return answer;
        }

        // The answer's peer, when it names one, must be this player.
        if (const auto named = answer->Object.find("peer"); named != answer->Object.end() && named->is_object())
        {
            const std::string clientId = TextOf(*named, "client_id");

            if (!clientId.empty() && !SameId(clientId, peer.ClientId))
            {
                return std::unexpected(MCError{.Message = "The lobby registered another player.",
                                               .Code = std::string(IdentityChangedCode)});
            }
        }

        if (const std::string token = TextOf(answer->Object, "peer_token"); IsHex(token, TokenDigits))
        {
            peer.PeerToken = token;
        }

        if (!IsHex(peer.PeerToken, TokenDigits))
        {
            return std::unexpected(
                MCError{.Message = "The lobby gave no peer token.", .Code = std::string(InvalidResponseCode)});
        }

        return answer;
    }

    MCResult<MCSeat> MCService::JoinRoom(MCPeer& peer, const MCRoom& room, std::string_view password,
                                         std::string_view content, bool ready)
    {
        // The launcher's fields for a join, in its order: the room, then (a Redux room) the engine it plays.
        MCFields extra = {
            {"username", peer.Username},         {"room", room.Name},
            {"is_host", std::string(Zero)},      {"max_players", std::to_string(room.MaxPlayers)},
            {"password", std::string(password)}, {"is_ready", ready ? std::string(ReadyFlag) : std::string(Zero)}};

        if (IsHex(room.RoomId, ClientIdDigits))
        {
            extra.emplace_back("room_id", room.RoomId);
        }

        extra.insert(extra.end(), {{"engine", std::string(ReduxEngine)},
                                   {"engine_protocol", std::string(ReduxEngineProtocol)},
                                   {"engine_build", room.EngineBuild},
                                   {"engine_content", std::string(content)}});
        const auto answer = RegisterWith(peer, extra);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return MCSeat{.Room = room.Name,
                      .RoomId = room.RoomId,
                      .Host = false,
                      .Ready = ready,
                      .LastMessageId = NumberOf(answer->Object, "room_last_message_id", 0)};
    }

    MCResult<MCSeat> MCService::HostRoom(MCPeer& peer, std::string_view name, int32_t maxPlayers,
                                         std::string_view password, std::string_view build, std::string_view content)
    {
        const MCFields extra = {{"username", peer.Username},
                                {"room", std::string(name)},
                                {"is_host", std::string(HostFlag)},
                                {"max_players", std::to_string(maxPlayers)},
                                {"description", ""},
                                {"password", std::string(password)},
                                {"is_ready", std::string(ReadyFlag)},
                                {"engine", std::string(ReduxEngine)},
                                {"engine_protocol", std::string(ReduxEngineProtocol)},
                                {"engine_build", std::string(build)},
                                {"engine_content", std::string(content)}};
        const auto answer = RegisterWith(peer, extra);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        MCSeat seat{.Room = std::string(name),
                    .Host = true,
                    .Ready = true,
                    .LastMessageId = NumberOf(answer->Object, "room_last_message_id", 0)};

        if (const auto named = answer->Object.find("peer"); named != answer->Object.end() && named->is_object())
        {
            if (const std::string roomId = TextOf(*named, "room_id"); IsHex(roomId, ClientIdDigits))
            {
                seat.RoomId = roomId;
            }
        }

        return seat;
    }

    MCResult<MCRoomState> MCService::RoomPoll(MCPeer& peer, MCSeat& seat, std::string_view matchSession,
                                              bool matchRunning)
    {
        MCFields extra = {{"room", seat.Room}};

        if (IsHex(seat.RoomId, ClientIdDigits))
        {
            extra.emplace_back("room_id", seat.RoomId);
        }

        extra.insert(extra.end(), {{"last_id", std::to_string(seat.LastMessageId)},
                                   {"limit", std::string(RoomMessagesAsked)},
                                   {"mention_cursor", std::string(Zero)},
                                   {"is_ready", seat.Ready ? std::string(ReadyFlag) : std::string(Zero)}});

        if (IsHex(matchSession, ClientIdDigits))
        {
            extra.insert(extra.end(),
                         {{"redux_process_session", std::string(matchSession)},
                          {"redux_process_running", matchRunning ? std::string(ReadyFlag) : std::string(Zero)}});
        }

        const auto answer = AskChat("room_poll", extra, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        const Json& object = answer->Object;
        seat.LastMessageId = RoomCursorFrom(object, seat.LastMessageId);
        MCRoomState state;
        state.Players = PlayersFrom(object);

        if (const auto settings = object.find("room_settings"); settings != object.end() && settings->is_object())
        {
            state.Name = TextOf(*settings, "name", seat.Room);
            state.EngineBuild = TextOf(*settings, "engine_build");
            state.EngineContent = TextOf(*settings, "engine_content");
        }

        // The player's own registration says which room it is in: another (or none) is the room gone for it.
        bool elsewhere = false;

        if (const auto named = object.find("peer"); named != object.end() && named->is_object())
        {
            const std::string roomId = TextOf(*named, "room_id");

            if (IsHex(roomId, ClientIdDigits) && !IsHex(seat.RoomId, ClientIdDigits) && seat.Host)
            {
                seat.RoomId = roomId;
            }

            elsewhere = IsHex(seat.RoomId, ClientIdDigits) && IsHex(roomId, ClientIdDigits)
                            ? !SameId(roomId, seat.RoomId)
                            : TextOf(*named, "room", seat.Room) != seat.Room;
        }

        if (FlagOf(object, "removed") || elsewhere)
        {
            state.Gone = true;

            if (const auto reason = object.find("exit_reason"); reason != object.end() && reason->is_object())
            {
                state.GoneWhy = TextOf(*reason, "kind");
            }
        }

        if (const auto start = object.find("start_signal"); start != object.end() && start->is_object())
        {
            if (auto match = MatchFrom(*start))
            {
                state.Start = std::move(*match);
            }
        }

        return state;
    }

    MCResult<void> MCService::SetReady(MCPeer& peer, MCSeat& seat, bool ready)
    {
        const auto answer =
            AskChat("heartbeat", {{"is_ready", ready ? std::string(ReadyFlag) : std::string(Zero)}}, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        seat.Ready = ready;
        return {};
    }

    MCResult<MCMatch> MCService::CreateSession(MCPeer& peer, std::string_view build, std::string_view content)
    {
        // The room's identity as the launcher adds it (AddReduxRoomIdentity), then no countdown.
        const MCFields extra = {{"engine", std::string(ReduxEngine)},
                                {"engine_build", std::string(build)},
                                {"engine_content", std::string(content)},
                                {"launch_countdown", std::string(Zero)}};
        const auto answer = AskChat("create_session", extra, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return MatchFrom(answer->Object);
    }

    MCResult<void> MCService::PublishReady(MCPeer& peer, std::string_view session)
    {
        const auto answer = AskChat("publish_ready", {{"session_hex", std::string(session)}}, peer);
        return answer ? MCResult<void>() : std::unexpected(answer.error());
    }

    MCResult<void> MCService::CloseSession(MCPeer& peer, std::string_view session, std::string_view build,
                                           std::string_view content)
    {
        // The session, then (a match played) the room's engine, as the launcher's AddReduxRoomIdentity adds it.
        MCFields extra = {{"session_hex", std::string(session)}};

        if (!build.empty())
        {
            extra.insert(extra.end(), {{"engine", std::string(ReduxEngine)},
                                       {"engine_build", std::string(build)},
                                       {"engine_content", std::string(content)}});
        }

        const auto answer = AskChat("close_session", extra, peer);
        return answer ? MCResult<void>() : std::unexpected(answer.error());
    }

    MCResult<void> MCService::ReportSessionMissing(MCPeer& peer, std::string_view session)
    {
        const auto answer = AskChat("report_session_missing", {{"session_hex", std::string(session)}}, peer);
        return answer ? MCResult<void>() : std::unexpected(answer.error());
    }

    MCResult<MCAdmission> MCService::Ticket(MCPeer& peer, const MCMatch& match, std::string_view epoch, bool host)
    {
        const MCFields extra = {{"session_hex", match.SessionHex},
                                {"engine_build", match.EngineBuild},
                                {"engine_content", match.EngineContent},
                                {"relay_epoch", std::string(epoch)}};
        const auto answer = AskChat("redux_ticket", extra, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        // As the launcher holds the admission: for this match's relay, room and player, in this role, and a ticket of
        // the relay's characters.
        const auto admission = answer->Object.find("admission");
        const Json none = Json::object();
        const Json& given = admission != answer->Object.end() && admission->is_object() ? *admission : none;
        MCAdmission ticket{.Ticket = TextOf(given, "ticket"), .Role = TextOf(given, "role")};
        const bool ticketShaped =
            ticket.Ticket.size() >= TicketFewest && ticket.Ticket.size() <= TicketMost &&
            std::ranges::all_of(
                ticket.Ticket, [](char c)
                { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '.' || c == '_'; });
        const bool forThis =
            NumberOf(given, "protocol", 0) == EngineProtocol && TextOf(given, "host") == match.RelayHost &&
            NumberOf(given, "port", 0) == match.RelayPort && SameId(TextOf(given, "peer"), peer.ClientId) &&
            TextOf(given, "room") == match.SessionHex && TextOf(given, "room_id") == match.RoomId &&
            ticket.Role == (host ? HostRole : JoinRole);

        if (!ticketShaped || !forThis)
        {
            return std::unexpected(MCError{.Message = "The relay's admission couldn't be checked.",
                                           .Code = std::string(InvalidResponseCode)});
        }

        return ticket;
    }

    MCResult<void> MCService::LeaveRoom(MCPeer& peer)
    {
        return Register(peer);
    }

    MCResult<MCLobby> MCService::Lobby(MCPeer& peer)
    {
        const MCFields extra = {{"last_id", std::to_string(peer.LastMessageId)},
                                {"message_revision", std::to_string(peer.MessageRevision)},
                                {"is_ready", std::string(Zero)},
                                {"desktop_after", std::string(Zero)},
                                {"friend_notification_after", std::string(Zero)}};
        const auto answer = AskChat("lobby_poll", extra, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        FollowChat(answer->Object, peer);
        MCLobby lobby;

        if (const auto peers = answer->Object.find("peers"); peers != answer->Object.end() && peers->is_array())
        {
            lobby.Players = peers->size();
        }

        if (auto rooms = RoomsFrom(answer->Object))
        {
            lobby.Rooms = std::move(*rooms);
            return lobby;
        }

        const auto listed = AskChat("get_rooms", {}, peer);

        if (!listed)
        {
            return std::unexpected(listed.error());
        }

        lobby.Rooms = RoomsFrom(listed->Object).value_or(std::vector<MCRoom>());
        return lobby;
    }

    MCResult<std::optional<std::string>> MCService::StockBuild(MCPeer& peer)
    {
        const auto answer = AskChat("redux_stock_status", {}, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        const std::string build = TextOf(answer->Object, "stock_pilot_build");
        return build.empty() ? std::nullopt : std::optional<std::string>(build);
    }

    MCResult<void> MCService::Unregister(MCPeer& peer)
    {
        const auto answer = AskChat(UnregisterAction, {}, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        return {};
    }

    MCResult<MCService::MCAnswer> MCService::AskAccount(std::string body)
    {
        auto answer = Post(_Client.Account, JsonType, body);
        Wipe(body);
        return answer;
    }

    MCResult<MCService::MCAnswer> MCService::AskChat(std::string_view action, const MCFields& extra,
                                                     MCOptionalRef<MCPeer> peer)
    {
        auto answer = AskChatOnce(action, extra, peer);

        if (answer || answer.error().Code != PeerExpiredCode || !peer || action == RecoverAction)
        {
            return answer;
        }

        if (const auto recovered = Recover(peer->get()); !recovered)
        {
            return std::unexpected(recovered.error());
        }

        return AskChatOnce(action, extra, peer);
    }

    MCResult<MCService::MCAnswer> MCService::AskChatOnce(std::string_view action, const MCFields& extra,
                                                         MCOptionalRef<MCPeer> peer)
    {
        MCFields fields = {{"action", std::string(action)}};

        if (peer)
        {
            const MCPeer& who = peer->get();
            fields.insert(fields.end(), {{"client_id", who.ClientId},
                                         {"peer_token", who.PeerToken},
                                         {"account_token", who.AccountToken},
                                         {"client_version", _Client.ClientVersion},
                                         {"room_event_version", std::string(RoomEventVersion)},
                                         {"last_notification_id", std::string(Zero)}});
        }

        fields.insert(fields.end(), extra.begin(), extra.end());
        fields.insert(fields.end(), {{"device_id", _Client.DeviceId},
                                     {"redux_protocol", std::string(ReduxProtocol)},
                                     {"redux_stock_protocol", std::string(ReduxStockProtocol)}});

        if (peer && !Among(action, NoIdentityActions))
        {
            fields.emplace_back("identity_ids", peer->get().ClientId);
        }

        if (Among(action, SystemActions))
        {
            fields.emplace_back("client_os", _Client.ClientOs);
        }

        std::string body = FormBody(fields);
        auto answer = Post(_Client.Chat, FormType, body);
        Wipe(body);
        return answer;
    }

    MCResult<void> MCService::Recover(MCPeer& peer)
    {
        // A player the lobby never gave a token has none to renew (the launcher's PeerRecovery).
        if (!IsHex(peer.PeerToken, TokenDigits))
        {
            return std::unexpected(MCError{.Message = "The lobby's session lapsed, and there's no token to renew.",
                                           .Code = std::string(PeerExpiredCode)});
        }

        const auto answer = AskChatOnce(RecoverAction, {}, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
        }

        const auto named = answer->Object.find("peer");

        if (named == answer->Object.end() || !SameId(TextOf(*named, "client_id"), peer.ClientId))
        {
            return std::unexpected(MCError{.Message = "The lobby renewed another player's session.",
                                           .Code = std::string(IdentityChangedCode)});
        }

        if (const std::string token = TextOf(answer->Object, "peer_token"); IsHex(token, TokenDigits))
        {
            peer.PeerToken = token;
        }

        return {};
    }

    MCResult<MCService::MCAnswer> MCService::Post(const std::string& address, std::string_view contentType,
                                                  std::string& body)
    {
        MCHttpSource::MCPost post{
            .ContentType = std::string(contentType), .Body = body, .UserAgent = _Client.UserAgent};
        MCGather reply;
        const auto sent = _Transfer.Post(address, post, reply);
        Wipe(post.Body);

        if (!sent)
        {
            return std::unexpected(
                MCError{.Message = std::format("MechCommander Online couldn't be reached: {}", sent.error().Reason),
                        .Code = std::string(UnreachableCode),
                        .Unreachable = sent.error().Unreachable});
        }

        if (reply.TooLong)
        {
            return std::unexpected(MCError{.Message = "MechCommander Online's answer is too long.",
                                           .Code = std::string(TooLongCode),
                                           .Status = reply.Code});
        }

        Json object = Json::parse(reply.Text, nullptr, false);

        if (object.is_discarded())
        {
            return std::unexpected(NotMcos(reply.Code, "isn't JSON"));
        }

        if (!object.is_object())
        {
            return std::unexpected(NotMcos(reply.Code, "isn't an object"));
        }

        const auto success = object.find("success");
        const bool succeeded = success != object.end() && success->is_boolean() && success->get<bool>();

        if (reply.Code >= HttpOkFirst && reply.Code <= HttpOkLast && succeeded)
        {
            return MCAnswer{std::move(object)};
        }

        MCError error{.Message = TextOf(object, "error", RefusedText),
                      .Code = TextOf(object, "code"),
                      .Status = reply.Code,
                      .RetryAfter = CountOf(NumberOf(object, "retry_after", 0))};

        if (error.Code.empty() && error.Message == PeerExpiredText)
        {
            error.Code = std::string(PeerExpiredCode);
        }

        return std::unexpected(std::move(error));
    }
}
