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
        const auto answer = AskChat(RegisterAction, extra, peer);

        if (!answer)
        {
            return std::unexpected(answer.error());
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

        return {};
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
