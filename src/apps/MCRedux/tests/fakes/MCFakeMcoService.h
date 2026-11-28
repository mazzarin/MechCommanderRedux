#pragma once

#include "network/MCMcoApi.h"
#include "network/MCMcoKeys.h"
#include "network/MCMcoRooms.h"
#include "update/MCHttpSource.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <charconv>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Port-only (Redux HD): a stand-in for MechCommander Online's two services in the process (network/MCMcoApi.h, as its
// launcher speaks to them), for the game's own MCO (network/MCMcoRooms.h): an account to sign in to, the lobby's
// players and rooms, joining and hosting, READY, a room's match started and its tickets, each answer shaped as MCO's.
// Several players' games may ask it at once (each from its own thread): one lock guards it.
namespace MCFakeMcoService
{
    using Json = nlohmann::json;

    /// <summary>A form's body as its fields, in order (its encoding undone).</summary>
    inline std::vector<std::pair<std::string, std::string>> FormFields(std::string_view body)
    {
        constexpr int HexBase = 16;
        const auto decode = [](std::string_view text)
        {
            std::string decoded;

            for (size_t i = 0; i < text.size(); i++)
            {
                if (text[i] == '+')
                {
                    decoded += ' ';
                }
                else if (text[i] == '%' && i + 2 < text.size())
                {
                    decoded += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, HexBase));
                    i += 2;
                }
                else
                {
                    decoded += text[i];
                }
            }

            return decoded;
        };

        std::vector<std::pair<std::string, std::string>> fields;

        while (!body.empty())
        {
            const size_t end = std::min(body.find('&'), body.size());
            const std::string_view field = body.substr(0, end);
            const size_t equals = field.find('=');
            fields.emplace_back(decode(field.substr(0, equals)), decode(field.substr(equals + 1)));
            body.remove_prefix(std::min(end + 1, body.size()));
        }

        return fields;
    }

    inline constexpr std::string_view ChatUrl = "https://mco.test/chat_api.php";
    inline constexpr std::string_view AccountUrl = "https://mco.test/account_api.php";
    inline constexpr std::string_view StockBuild = "official-0.2.1.61";
    inline constexpr std::string_view RelayHost = "relay.mco.test";
    inline constexpr int RelayPort = 8443;
    inline constexpr std::string_view Email = "pilot@example.com";
    inline constexpr std::string_view Password = "right password";
    inline constexpr std::string_view AccountName = "PILOT";
    inline constexpr int HttpOk = 200;
    inline constexpr int HttpUnauthorized = 401;
    inline constexpr int HttpConflict = 409;
    inline constexpr int HttpForbidden = 403;
    /// <summary>A relay's ticket's least length (the launcher's), each role's its own letter.</summary>
    inline constexpr size_t TicketLength = 64;
    /// <summary>The room's chat lines a poll brings at most (as room_poll's limit asks).</summary>
    inline constexpr size_t MessagesPerPoll = 50;
    /// <summary>The name MCO's own chat lines carry.</summary>
    inline constexpr std::string_view SystemName = "System";

    /// <summary>The stand-in (see the file's comment).</summary>
    class MCService final : public MCHttpSource::MCGet
    {
    public:
        /// <summary>A player as the lobby knows them.</summary>
        struct MCPeer
        {
            std::string ClientId;
            std::string PeerToken;
            std::string Username;
            std::string RoomId;
            bool Host = false;
            bool Ready = false;
        };

        /// <summary>A room hosted.</summary>
        struct MCRoom
        {
            std::string RoomId;
            std::string Name;
            std::string HostClient;
            int32_t MaxPlayers = MCMcoApi::RoomMost;
            std::string Password;
            std::string Build;
            std::string Content;
            /// <summary>What update_room last said of it (the mission, for our hosts).</summary>
            std::string Description;
            std::optional<Json> Match;
            /// <summary>The host said its game is hosting the match (publish_ready): the guests are told of it.</summary>
            bool Published = false;
            /// <summary>The room's chat, oldest first, each line as room_poll gives it.</summary>
            std::vector<Json> Messages;
        };

        /// <summary>Why a player is out of their room (room_poll's exit_reason).</summary>
        struct MCExit
        {
            std::string Kind;
            std::string By;
        };

        /// <summary>A transfer for one game's MCO, asking this stand-in (the setup's Get makes one each).</summary>
        class MCTransfer final : public MCHttpSource::MCGet
        {
        public:
            explicit MCTransfer(MCService& service) : _Service(service) {}

            std::expected<void, MCHttpSource::MCGetError> Get(const std::string& url, uint64_t from,
                                                              MCHttpSource::MCReply& reply) override
            {
                return _Service.Get(url, from, reply);
            }

            std::expected<void, MCHttpSource::MCGetError> Post(const std::string& url, const MCHttpSource::MCPost& post,
                                                               MCHttpSource::MCReply& reply) override
            {
                return _Service.Post(url, post, reply);
            }

        private:
            MCService& _Service;
        };

        // What the tests set.

        /// <summary>The account is signed in on another computer: activate refuses without take_over.</summary>
        std::atomic<bool> ActiveElsewhere = false;
        /// <summary>An account may be made from here (creation_status).</summary>
        std::atomic<bool> CanCreate = true;
        /// <summary>MCO can't be reached.</summary>
        std::atomic<bool> Unreachable = false;
        /// <summary>A room another launcher hosts, in the lobby beside ours (its build and content as given).</summary>
        void AddRoom(MCRoom room)
        {
            const std::scoped_lock lock(_Lock);
            _Rooms[room.RoomId] = std::move(room);
        }

        /// <summary>A player in <paramref name="roomId"/> who isn't one of the games tested (READY or not).</summary>
        void AddPlayer(std::string_view roomId, std::string username, bool ready, bool host = false)
        {
            const std::scoped_lock lock(_Lock);
            const std::string id = NextHex();
            _Peers[id] = {.ClientId = id,
                          .PeerToken = NextHex(MCMcoApi::TokenDigits),
                          .Username = std::move(username),
                          .RoomId = std::string(roomId),
                          .Host = host,
                          .Ready = ready};

            if (const auto room = _Rooms.find(std::string(roomId)); host && room != _Rooms.end())
            {
                room->second.HostClient = id;
            }
        }

        /// <summary>
        /// The room's host (another launcher's, as <see cref="AddPlayer"/> made it) STARTs its match, its game hosting
        /// it: the guests hear of it from their polls' start_signal.
        /// </summary>
        void StartMatch(std::string_view roomId)
        {
            const std::scoped_lock lock(_Lock);

            if (const auto room = _Rooms.find(std::string(roomId)); room != _Rooms.end())
            {
                MakeMatch(room->first, room->second);
                room->second.Published = true;
            }
        }

        /// <summary>The room's host leaves: its room closes for everyone in it.</summary>
        void HostLeaves(std::string_view roomId)
        {
            const std::scoped_lock lock(_Lock);
            CloseRoom(std::string(roomId), "host_connection_lost");
        }

        /// <summary>MCO refuses the chat's lines (send_message), saying so.</summary>
        std::atomic<bool> ChatRefused = false;

        /// <summary><paramref name="username"/> in <paramref name="roomId"/> says <paramref name="text"/> (a text line).</summary>
        void Say(std::string_view roomId, std::string_view username, std::string text)
        {
            const std::scoped_lock lock(_Lock);
            AppendLine(std::string(roomId), {{"sender_client_id", ClientOf(username)},
                                             {"username", username},
                                             {"message", std::move(text)},
                                             {"type", "text"}});
        }

        /// <summary>A line of <paramref name="fields"/> in <paramref name="roomId"/>'s chat (a picture's, MCO's own).</summary>
        void AddLine(std::string_view roomId, Json fields)
        {
            const std::scoped_lock lock(_Lock);
            AppendLine(std::string(roomId), std::move(fields));
        }

        /// <summary>
        /// <paramref name="by"/> takes <paramref name="username"/> out of <paramref name="roomId"/> (kick_peer): its
        /// next poll says so, and the room's chat.
        /// </summary>
        void Kick(std::string_view roomId, std::string_view username, std::string by)
        {
            const std::scoped_lock lock(_Lock);

            for (auto& [id, peer] : _Peers)
            {
                if (peer.RoomId == roomId && peer.Username == username)
                {
                    peer.RoomId.clear();
                    _Removed[id] = {.Kind = "kicked", .By = std::move(by)};
                    AppendLine(std::string(roomId), Event("removed", username));
                    return;
                }
            }
        }

        /// <summary>The room's chat as it stands (each line's text, or MCO's own line's event and player).</summary>
        std::vector<Json> MessagesIn(std::string_view roomId) const
        {
            const std::scoped_lock lock(_Lock);
            const auto room = _Rooms.find(std::string(roomId));
            return room == _Rooms.end() ? std::vector<Json>{} : room->second.Messages;
        }

        /// <summary>The actions asked, in order.</summary>
        std::vector<std::string> Actions() const
        {
            const std::scoped_lock lock(_Lock);
            return _Actions;
        }

        /// <summary>How many times <paramref name="action"/> was asked.</summary>
        size_t Asked(std::string_view action) const
        {
            const std::scoped_lock lock(_Lock);
            return static_cast<size_t>(std::ranges::count(_Actions, action));
        }

        /// <summary>The fields of the last <paramref name="action"/> asked (a form's, or the JSON's as text).</summary>
        std::map<std::string, std::string> LastFields(std::string_view action) const
        {
            const std::scoped_lock lock(_Lock);
            const auto last = _Last.find(std::string(action));
            return last == _Last.end() ? std::map<std::string, std::string>{} : last->second;
        }

        /// <summary>The fields of the last <paramref name="action"/> the player named <paramref name="username"/> asked.</summary>
        std::map<std::string, std::string> LastFieldsFrom(std::string_view action, std::string_view username) const
        {
            const std::scoped_lock lock(_Lock);

            for (const auto& [id, peer] : _Peers)
            {
                const auto asked = _LastFrom.find({std::string(action), id});

                if (peer.Username == username && asked != _LastFrom.end())
                {
                    return asked->second;
                }
            }

            return {};
        }

        /// <summary>The sessions closed, each with the engine it was closed with ("" for one called off).</summary>
        std::vector<std::pair<std::string, std::string>> Closed() const
        {
            const std::scoped_lock lock(_Lock);
            return _Closed;
        }

        /// <summary>The rooms hosted or added, by their ids.</summary>
        /// <summary>The room <paramref name="roomId"/> as it stands, if it's there.</summary>
        std::optional<MCRoom> RoomOf(std::string_view roomId) const
        {
            const std::scoped_lock lock(_Lock);
            const auto room = _Rooms.find(std::string(roomId));
            return room != _Rooms.end() ? std::optional<MCRoom>(room->second) : std::nullopt;
        }

        /// <summary>Whether <paramref name="username"/> sits in <paramref name="roomId"/>, and READY.</summary>
        std::optional<bool> SeatOf(std::string_view roomId, std::string_view username) const
        {
            const std::scoped_lock lock(_Lock);

            for (const auto& [id, peer] : _Peers)
            {
                if (peer.RoomId == roomId && peer.Username == username)
                {
                    return peer.Ready;
                }
            }

            return std::nullopt;
        }

        std::vector<std::string> RoomIds() const
        {
            const std::scoped_lock lock(_Lock);
            std::vector<std::string> ids;

            for (const auto& [id, room] : _Rooms)
            {
                ids.push_back(id);
            }

            return ids;
        }

        // MCHttpSource::MCGet.

        std::expected<void, MCHttpSource::MCGetError> Get(const std::string& /*url*/, uint64_t /*from*/,
                                                          MCHttpSource::MCReply& /*reply*/) override
        {
            return std::unexpected(MCHttpSource::MCGetError{.Reason = "MCO's services are only posted to"});
        }

        std::expected<void, MCHttpSource::MCGetError> Post(const std::string& url, const MCHttpSource::MCPost& post,
                                                           MCHttpSource::MCReply& reply) override
        {
            const std::scoped_lock lock(_Lock);

            if (Unreachable)
            {
                return std::unexpected(MCHttpSource::MCGetError{.Unreachable = true, .Reason = "no connection"});
            }

            std::map<std::string, std::string> fields;

            if (url == AccountUrl)
            {
                const Json body = Json::parse(post.Body, nullptr, false);

                for (const auto& [key, value] : body.items())
                {
                    fields[key] = value.is_string() ? value.get<std::string>() : value.dump();
                }
            }
            else
            {
                for (auto& [key, value] : FormFields(post.Body))
                {
                    fields[key] = std::move(value);
                }
            }

            const std::string action = fields["action"];
            _Actions.push_back(action);
            _Last[action] = fields;
            _LastFrom[{action, fields["client_id"]}] = fields;
            const auto [status, answer] = url == AccountUrl ? Account(action, fields) : Chat(action, fields);

            if (reply.Status(status, {}))
            {
                reply.Body(answer.dump());
            }

            return {};
        }

    private:
        using MCFields = std::map<std::string, std::string>;
        using MCAnswer = std::pair<int, Json>;

        static Json Success(Json fields = Json::object())
        {
            fields["success"] = true;
            return fields;
        }

        static MCAnswer Error(int status, std::string_view code, std::string_view text)
        {
            return {status, Json{{"success", false}, {"code", code}, {"error", text}}};
        }

        std::string NextHex(size_t digits = MCMcoApi::ClientIdDigits)
        {
            constexpr std::string_view Digits = "0123456789abcdef";
            std::string hex(digits, '0');
            uint64_t n = ++_Made;

            for (size_t i = digits; i-- > 0 && n != 0; n /= Digits.size())
            {
                hex[i] = Digits[n % Digits.size()];
            }

            return hex;
        }

        Json AccountJson() const { return Json{{"client_id", _AccountClient}, {"email", Email}}; }

        Json SignInJson()
        {
            _AccountToken = NextHex(MCMcoApi::TokenDigits);
            return Success({{"account_token", _AccountToken},
                            {"expires_at", 1'900'000'000},
                            {"account", AccountJson()},
                            {"profile", {{"username", AccountName}, {"player_id", 42}}}});
        }

        MCAnswer Account(const std::string& action, MCFields& fields)
        {
            if (action == "login")
            {
                if (fields["email"] != Email || fields["password"] != Password)
                {
                    return Error(HttpUnauthorized, "invalid_credentials", "Invalid email or password.");
                }

                return {HttpOk, SignInJson()};
            }

            if (action == "session")
            {
                if (fields["account_token"] != _AccountToken || _AccountToken.empty())
                {
                    return Error(HttpUnauthorized, "account_session_invalid", "The session has ended.");
                }

                return {HttpOk, Success({{"account", AccountJson()}, {"profile", {{"username", AccountName}}}})};
            }

            if (action == "activate")
            {
                if (ActiveElsewhere && fields["take_over"] != "true")
                {
                    return Error(HttpConflict, MCMcoApi::ActiveDeviceCode, "Signed in on another device.");
                }

                ActiveElsewhere = false;
                const std::string token = NextHex(MCMcoApi::TokenDigits);
                _Peers[_AccountClient] = {
                    .ClientId = _AccountClient, .PeerToken = token, .Username = std::string(AccountName)};
                return {HttpOk, Success({{"peer_token", token}})};
            }

            if (action == "logout")
            {
                _AccountToken.clear();
                return {HttpOk, Success()};
            }

            if (action == "creation_status")
            {
                return {HttpOk, Success({{"available", CanCreate.load()}})};
            }

            if (action == "create" || action == "secure")
            {
                _AccountClient = fields["client_id"].empty() ? NextHex() : fields["client_id"];
                return {HttpOk, SignInJson()};
            }

            return Error(HttpForbidden, "unknown_action", "Unknown action.");
        }

        /// <summary>The room's match as create_session makes it, for the players in it now.</summary>
        void MakeMatch(const std::string& roomId, MCRoom& room)
        {
            int32_t players = 0;

            for (const auto& [id, each] : _Peers)
            {
                players += each.RoomId == roomId ? 1 : 0;
            }

            room.Match = Json{{"engine_protocol", MCMcoApi::EngineProtocol},
                              {"session_hex", NextHex()},
                              {"room_id", roomId},
                              {"engine_build", room.Build},
                              {"engine_content", room.Content},
                              {"stock_host", RelayHost},
                              {"stock_port", RelayPort},
                              {"capacity", players}};
        }

        Json RoomJson(const MCRoom& room) const
        {
            int32_t players = 0;
            std::string host;

            for (const auto& [id, peer] : _Peers)
            {
                players += peer.RoomId == room.RoomId ? 1 : 0;
                host = id == room.HostClient ? peer.Username : host;
            }

            return {{"room_id", room.RoomId},
                    {"name", room.Name},
                    {"host", host},
                    {"engine", MCMcoApi::ReduxEngine},
                    {"engine_build", room.Build},
                    {"engine_content", room.Content},
                    {"player_count", players},
                    {"max_players", room.MaxPlayers},
                    {"in_game", room.Match.has_value()},
                    {"locked", !room.Password.empty()},
                    {"admission_closed", false}};
        }

        Json PeerJson(const MCPeer& peer) const
        {
            const auto room = _Rooms.find(peer.RoomId);
            return {{"client_id", peer.ClientId},
                    {"room_id", peer.RoomId},
                    {"room", room == _Rooms.end() ? std::string(MCMcoApi::LobbyRoom) : room->second.Name}};
        }

        void CloseRoom(const std::string& roomId, std::string why)
        {
            for (auto& [id, peer] : _Peers)
            {
                if (peer.RoomId == roomId)
                {
                    peer.RoomId.clear();
                    _Removed[id] = {.Kind = why};
                }
            }

            _Rooms.erase(roomId);
        }

        /// <summary>MCO's own line of <paramref name="event"/> about <paramref name="player"/>.</summary>
        static Json Event(std::string_view event, std::string_view player)
        {
            return {{"sender_client_id", ""},
                    {"username", SystemName},
                    {"message", ""},
                    {"room_event", event},
                    {"event_player", player}};
        }

        /// <summary>A line in the room's chat, numbered on from MCO's last (under the lock).</summary>
        void AppendLine(const std::string& roomId, Json fields)
        {
            if (const auto room = _Rooms.find(roomId); room != _Rooms.end())
            {
                fields["id"] = ++_MessageId;
                room->second.Messages.push_back(std::move(fields));
            }
        }

        /// <summary>update_room, kick_peer or transfer_host from <paramref name="peer"/>, which must host <paramref name="room"/>.</summary>
        MCAnswer HostCall(const std::string& action, MCFields& fields, MCPeer& peer, MCRoom& room)
        {
            if (room.HostClient != peer.ClientId)
            {
                return Error(HttpForbidden, "not_host", "Only the host can do that.");
            }

            if (action == "update_room")
            {
                room.Name = fields["new_name"];
                room.Description = fields["description"];
                room.MaxPlayers = std::stoi(fields["max_players"]);
                return {HttpOk, Success()};
            }

            const auto target = _Peers.find(fields["target_client_id"]);

            if (target == _Peers.end() || target->second.RoomId != room.RoomId || target->first == peer.ClientId)
            {
                return Error(HttpForbidden, "peer_not_found", "That player isn't in the room.");
            }

            if (action == "kick_peer")
            {
                target->second.RoomId.clear();
                _Removed[target->first] = {.Kind = "kicked", .By = peer.Username};
                AppendLine(room.RoomId, Event("removed", target->second.Username));
                return {HttpOk, Success()};
            }

            room.HostClient = target->first;
            target->second.Host = true;
            peer.Host = false;
            return {HttpOk, Success()};
        }

        std::string ClientOf(std::string_view username) const
        {
            for (const auto& [id, peer] : _Peers)
            {
                if (peer.Username == username)
                {
                    return id;
                }
            }

            return {};
        }

        MCAnswer Chat(const std::string& action, MCFields& fields)
        {
            const std::string client = fields["client_id"];

            if (action == "register_peer")
            {
                MCPeer& peer = _Peers[client];
                const bool fresh = peer.PeerToken.empty();
                peer.ClientId = client;
                peer.Username = fields["username"];
                peer.PeerToken = fresh ? NextHex(MCMcoApi::TokenDigits) : peer.PeerToken;
                _Removed.erase(client);

                // Out of a room it hosted: the room closes for the others; a guest's leaving is said in its chat.
                if (peer.Host && !peer.RoomId.empty())
                {
                    peer.Host = false;
                    CloseRoom(std::exchange(peer.RoomId, {}), "closed");
                }
                else if (!peer.RoomId.empty())
                {
                    AppendLine(peer.RoomId, Event("left", peer.Username));
                }

                peer.RoomId.clear();
                peer.Host = false;

                if (fields["is_host"] == "1")
                {
                    const std::string id = NextHex();
                    _Rooms[id] = {.RoomId = id,
                                  .Name = fields["room"],
                                  .HostClient = client,
                                  .MaxPlayers = std::stoi(fields["max_players"]),
                                  .Password = fields["password"],
                                  .Build = fields["engine_build"],
                                  .Content = fields["engine_content"]};
                    peer.RoomId = id;
                    peer.Host = true;
                    peer.Ready = true;
                }
                else if (fields["room"] != MCMcoApi::LobbyRoom)
                {
                    const auto room = _Rooms.find(fields["room_id"]);

                    if (room == _Rooms.end())
                    {
                        return Error(HttpForbidden, "room_not_found", "That room is gone.");
                    }

                    if (room->second.Password != fields["password"])
                    {
                        return Error(HttpForbidden, "wrong_password", "Wrong room password.");
                    }

                    peer.RoomId = room->first;
                    peer.Ready = fields["is_ready"] == "1";
                    AppendLine(room->first, Event("joined", peer.Username));
                }

                return {HttpOk, Success({{"peer_token", peer.PeerToken}, {"peer", PeerJson(peer)}})};
            }

            const auto known = _Peers.find(client);

            if (known == _Peers.end() || known->second.PeerToken != fields["peer_token"])
            {
                return Error(HttpUnauthorized, "peer_auth_failed", "Unknown player.");
            }

            MCPeer& peer = known->second;

            if (action == "unregister_peer")
            {
                _Peers.erase(known);
                return {HttpOk, Success()};
            }

            if (action == "redux_stock_status")
            {
                return {HttpOk, Success({{"stock_pilot_build", StockBuild}})};
            }

            if (action == "lobby_poll" || action == "get_rooms")
            {
                Json rooms = Json::array();
                Json peers = Json::array();

                for (const auto& [id, room] : _Rooms)
                {
                    rooms.push_back(RoomJson(room));
                }

                for (const auto& [id, each] : _Peers)
                {
                    peers.push_back({{"client_id", id}, {"username", each.Username}});
                }

                return {HttpOk, Success({{"rooms", rooms}, {"peers", peers}})};
            }

            if (action == "room_poll")
            {
                if (const auto removed = _Removed.find(client); removed != _Removed.end())
                {
                    Json reason = {{"kind", removed->second.Kind}};

                    if (!removed->second.By.empty())
                    {
                        reason["by"] = removed->second.By;
                    }

                    return {HttpOk, Success({{"removed", true}, {"exit_reason", reason}})};
                }

                const auto room = _Rooms.find(peer.RoomId);

                if (room == _Rooms.end())
                {
                    return {HttpOk, Success({{"peer", PeerJson(peer)}})};
                }

                Json players = Json::array();

                for (const auto& [id, each] : _Peers)
                {
                    if (each.RoomId == peer.RoomId)
                    {
                        players.push_back({{"client_id", id},
                                           {"username", each.Username},
                                           {"is_ready", each.Ready ? 1 : 0},
                                           {"is_host", id == room->second.HostClient ? 1 : 0}});
                    }
                }

                Json answer = Success({{"peers", players},
                                       {"room_settings",
                                        {{"name", room->second.Name},
                                         {"engine_build", room->second.Build},
                                         {"engine_content", room->second.Content}}},
                                       {"peer", PeerJson(peer)}});

                if (room->second.Match && room->second.Published && !peer.Host)
                {
                    answer["start_signal"] = *room->second.Match;
                }

                // The chat after the poll's last_id: its newest lines when there are more than a poll brings.
                int64_t after = 0;
                std::from_chars(fields["last_id"].data(), fields["last_id"].data() + fields["last_id"].size(), after);
                Json messages = Json::array();

                for (const Json& message : room->second.Messages)
                {
                    if (message["id"].get<int64_t>() > after)
                    {
                        messages.push_back(message);
                    }
                }

                if (messages.size() > MessagesPerPoll)
                {
                    messages.erase(messages.begin(), messages.end() - static_cast<std::ptrdiff_t>(MessagesPerPoll));
                }

                answer["messages"] = messages;
                answer["last_id"] = _MessageId;
                return {HttpOk, answer};
            }

            if (action == "heartbeat")
            {
                peer.Ready = fields["is_ready"] == "1";
                return {HttpOk, Success()};
            }

            const auto room = _Rooms.find(peer.RoomId);

            if (room == _Rooms.end())
            {
                return Error(HttpForbidden, "not_in_room", "Not in a room.");
            }

            // The host's own calls (Port-only, Redux HD: SPEC-mco-hosting.md), as the launcher's room window makes them.
            if (action == "update_room" || action == "kick_peer" || action == "transfer_host")
            {
                return HostCall(action, fields, peer, room->second);
            }

            if (action == "send_message")
            {
                if (ChatRefused)
                {
                    return Error(HttpForbidden, "chat_blocked", "You can't chat in this room.");
                }

                AppendLine(room->first, {{"sender_client_id", client},
                                         {"username", peer.Username},
                                         {"message", fields["message"]},
                                         {"request_id", fields["request_id"]},
                                         {"type", "text"}});
                return {HttpOk, Success()};
            }

            if (action == "create_session")
            {
                MakeMatch(room->first, room->second);
                return {HttpOk, Success(*room->second.Match)};
            }

            if (action == "publish_ready")
            {
                room->second.Published = room->second.Match.has_value();
                return {HttpOk, Success()};
            }

            if (action == "report_session_missing")
            {
                return {HttpOk, Success()};
            }

            if (action == "close_session")
            {
                _Closed.emplace_back(fields["session_hex"], fields["engine_build"]);
                room->second.Match.reset();
                room->second.Published = false;
                return {HttpOk, Success()};
            }

            if (action == "redux_ticket")
            {
                if (!room->second.Match)
                {
                    return Error(HttpForbidden, "session_missing", "The session is no longer available.");
                }

                const bool host = room->second.HostClient == client;
                return {HttpOk, Success({{"admission",
                                          {{"protocol", MCMcoApi::EngineProtocol},
                                           {"host", RelayHost},
                                           {"port", RelayPort},
                                           {"peer", client},
                                           {"room", (*room->second.Match)["session_hex"]},
                                           {"room_id", room->first},
                                           {"role", host ? "host" : "join"},
                                           {"ticket", std::string(TicketLength, host ? 'H' : 'J')}}}})};
            }

            return Error(HttpForbidden, "unknown_action", "Unknown action.");
        }

        mutable std::mutex _Lock;
        uint64_t _Made = 0;
        std::string _AccountClient = std::string(MCMcoApi::ClientIdDigits, 'c');
        std::string _AccountToken;
        std::map<std::string, MCPeer> _Peers;
        std::map<std::string, MCRoom> _Rooms;
        std::map<std::string, MCExit> _Removed;
        int64_t _MessageId = 0;
        std::vector<std::string> _Actions;
        std::map<std::string, MCFields> _Last;
        std::map<std::pair<std::string, std::string>, MCFields> _LastFrom;
        std::vector<std::pair<std::string, std::string>> _Closed;
    };

    /// <summary>The sign-in's store as the tests keep it: in memory.</summary>
    class MCMemoryKeys final : public MCMcoKeys
    {
    public:
        std::optional<MCMcoKept> Load() override
        {
            const std::scoped_lock lock(_Lock);
            return _Kept;
        }

        bool Save(const MCMcoKept& kept) override
        {
            const std::scoped_lock lock(_Lock);
            _Kept = kept;
            return true;
        }

        void Erase() override
        {
            const std::scoped_lock lock(_Lock);
            _Kept.reset();
        }

        std::optional<MCMcoKept> Now() const
        {
            const std::scoped_lock lock(_Lock);
            return _Kept;
        }

    private:
        mutable std::mutex _Lock;
        std::optional<MCMcoKept> _Kept;
    };

    /// <summary>The game data's fingerprint the tests' games have, and another's.</summary>
    inline const std::string Content(MCMcoApi::TokenDigits, '9');
    inline const std::string OtherContent(MCMcoApi::TokenDigits, '8');

    /// <summary>
    /// MCO in a test's game, asking <paramref name="mco"/>, its match's relay dialled by <paramref name="dial"/> (none:
    /// no match), its polls every <paramref name="pollLeast"/> to <paramref name="pollMost"/>.
    /// </summary>
    inline MCMcoSetup Setup(MCService& mco, std::shared_ptr<MCMcoKeys> keys,
                            std::function<MCMcoBridge::MCDial(const MCMcoApi::MCMatch&)> dial = {},
                            std::string content = Content, std::chrono::milliseconds pollLeast = MCMcoSetup{}.PollLeast,
                            std::chrono::milliseconds pollMost = MCMcoSetup{}.PollMost)
    {
        return MCMcoSetup{.Get = [&mco] { return std::make_unique<MCService::MCTransfer>(mco); },
                          .Client = {.DeviceId = std::string(MCMcoApi::TokenDigits, 'e'),
                                     .UserAgent = "MechCommanderReduxHD/test (macOS 26.5)",
                                     .ClientOs = "{}",
                                     .Chat = std::string(ChatUrl),
                                     .Account = std::string(AccountUrl)},
                          .Keys = std::move(keys),
                          .Dial = std::move(dial),
                          .Content = [content] { return std::expected<std::string, std::string>(content); },
                          .PollLeast = pollLeast,
                          .PollMost = pollMost};
    }

    /// <summary>A room another launcher hosts on MCO's stock build, with <paramref name="content"/>'s data.</summary>
    inline MCService::MCRoom OthersRoom(std::string id, std::string name, std::string content = Content)
    {
        return {.RoomId = std::move(id),
                .Name = std::move(name),
                .Build = std::string(StockBuild),
                .Content = std::move(content)};
    }
}
