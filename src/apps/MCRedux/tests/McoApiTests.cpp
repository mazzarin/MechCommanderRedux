#include "stdafx.h"
#include "../MCHttpGet.h"
#include "MCTest.h"
#include "fakes/MCFakeMcoService.h"
#include "lib/MCSha256.h"
#include "network/MCMcoApi.h"
#include "platform/MCSocket.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <format>
#include <map>
#include <thread>

// Port-only (Redux HD): MechCommander Online's services as network/MCMcoApi speaks them (SPEC-mco-rooms.md), against
// a stand-in for MCO's answers, and the platform's POST (MCHttpGet) against a server on this machine.

namespace
{
    using namespace std::chrono_literals;
    using Json = nlohmann::json;

    constexpr std::string_view ChatUrl = "https://mco.test/chat_api.php";
    constexpr std::string_view AccountUrl = "https://mco.test/account_api.php";
    constexpr std::string_view TestAgent = "MechCommanderReduxHD/0.6.0+1 (macOS 26.5)";
    constexpr std::string_view TestOs = R"({"family":"macOS"})";
    constexpr std::string_view FormType = "application/x-www-form-urlencoded";
    constexpr std::string_view JsonType = "application/json";
    constexpr int HttpOk = 200;
    constexpr int HttpForbidden = 403;
    constexpr int HttpTooMany = 429;
    constexpr int HttpFound = 302;

    /// <summary>Hex digits of the lengths MCO's ids and tokens have, each from its own digit.</summary>
    std::string Hex(char digit, size_t count)
    {
        return std::string(count, digit);
    }

    const std::string ClientId = Hex('c', MCMcoApi::ClientIdDigits);
    const std::string AccountToken = Hex('a', MCMcoApi::TokenDigits);
    const std::string PeerToken = Hex('b', MCMcoApi::TokenDigits);
    const std::string NewPeerToken = Hex('d', MCMcoApi::TokenDigits);
    const std::string DeviceId = Hex('e', MCMcoApi::TokenDigits);

    using MCFakeMcoService::FormFields;

    /// <summary>A form field's value, or none.</summary>
    std::optional<std::string> Field(std::string_view body, std::string_view name)
    {
        for (const auto& [key, value] : FormFields(body))
        {
            if (key == name)
            {
                return value;
            }
        }

        return std::nullopt;
    }

    /// <summary>
    /// The stand-in for MCO's services: answers each POST with the next scripted answer (a status and a body, or a
    /// failure to reach it), keeping what each asked.
    /// </summary>
    class MCFakeMco final : public MCHttpSource::MCGet
    {
    public:
        struct MCAsked
        {
            std::string Url;
            MCHttpSource::MCPost Post;
        };

        void Answer(int status, std::string body) { _Answers.push_back({status, std::move(body), false}); }

        void AnswerJson(const Json& body, int status = HttpOk) { Answer(status, body.dump()); }

        void Unreachable() { _Answers.push_back({0, {}, true}); }

        const std::vector<MCAsked>& Asked() const { return _Asked; }

        std::expected<void, MCHttpSource::MCGetError> Get(const std::string& /*url*/, uint64_t /*from*/,
                                                          MCHttpSource::MCReply& /*reply*/) override
        {
            return std::unexpected(MCHttpSource::MCGetError{.Reason = "MCO's services are only posted to"});
        }

        std::expected<void, MCHttpSource::MCGetError> Post(const std::string& url, const MCHttpSource::MCPost& post,
                                                           MCHttpSource::MCReply& reply) override
        {
            _Asked.push_back({url, post});

            if (_Answers.empty())
            {
                return std::unexpected(MCHttpSource::MCGetError{.Reason = "nothing scripted"});
            }

            const MCScripted answer = std::move(_Answers.front());
            _Answers.pop_front();

            if (answer.Unreachable)
            {
                return std::unexpected(MCHttpSource::MCGetError{.Unreachable = true, .Reason = "no connection"});
            }

            if (reply.Status(answer.Status, {}))
            {
                reply.Body(answer.Body);
            }

            return {};
        }

    private:
        struct MCScripted
        {
            int Status = 0;
            std::string Body;
            bool Unreachable = false;
        };

        std::deque<MCScripted> _Answers;
        std::vector<MCAsked> _Asked;
    };

    MCMcoApi::MCClient TestClient()
    {
        return {.DeviceId = DeviceId,
                .UserAgent = std::string(TestAgent),
                .ClientOs = std::string(TestOs),
                .Chat = std::string(ChatUrl),
                .Account = std::string(AccountUrl)};
    }

    Json Success(Json fields = Json::object())
    {
        fields["success"] = true;
        return fields;
    }

    /// <summary>A login's answer as MCO's account service gives it.</summary>
    Json LoginAnswer()
    {
        return Success({{"account_token", AccountToken},
                        {"expires_at", 1'900'000'000},
                        {"account", {{"client_id", ClientId}, {"email", "pilot@example.com"}}},
                        {"profile", {{"username", "PILOT"}, {"player_id", 42}}}});
    }

    MCMcoApi::MCPeer Registered()
    {
        return {.ClientId = ClientId, .PeerToken = PeerToken, .AccountToken = AccountToken, .Username = "PILOT"};
    }
}

TEST_CASE("mco api: a guest's registration in the lobby carries its player, the device, the system and the Redux "
          "protocols as MCO's launcher sends them, and keeps the peer token MCO gives")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(Success({{"peer_token", NewPeerToken}, {"peer", {{"client_id", ClientId}}}}));
    MCMcoApi::MCPeer guest{.ClientId = ClientId, .Username = "GUEST PILOT"};

    REQUIRE(service.Register(guest));
    CHECK_EQ(guest.PeerToken, NewPeerToken);
    REQUIRE_EQ(mco.Asked().size(), size_t{1});
    const auto& asked = mco.Asked()[0];
    CHECK_EQ(asked.Url, std::string(ChatUrl));
    CHECK_EQ(asked.Post.ContentType, std::string(FormType));
    CHECK_EQ(asked.Post.UserAgent, std::string(TestAgent));

    const std::vector<std::pair<std::string, std::string>> expected = {
        {"action", "register_peer"},
        {"client_id", ClientId},
        {"peer_token", ""},
        {"account_token", ""},
        {"client_version", std::string(MCMcoApi::LauncherRelease)},
        {"room_event_version", "2"},
        {"last_notification_id", "0"},
        {"username", "GUEST PILOT"},
        {"room", "Lobby"},
        {"is_host", "0"},
        {"max_players", "6"},
        {"status", "Online"},
        {"stream_url", ""},
        {"away_message", ""},
        {"device_id", DeviceId},
        {"redux_protocol", "1"},
        {"redux_stock_protocol", "2"},
        {"client_os", std::string(TestOs)},
    };

    CHECK(FormFields(asked.Post.Body) == expected);
}

TEST_CASE("mco api: a registration MCO answers for another player, or without a peer token, fails")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(Success({{"peer_token", NewPeerToken}, {"peer", {{"client_id", Hex('f', 32)}}}}));
    mco.AnswerJson(Success());
    MCMcoApi::MCPeer guest{.ClientId = ClientId, .Username = "GUEST"};

    const auto other = service.Register(guest);
    REQUIRE(!other);
    CHECK_EQ(other.error().Code, std::string("identity_changed"));
    CHECK(guest.PeerToken.empty());

    const auto none = service.Register(guest);
    REQUIRE(!none);
    CHECK_EQ(none.error().Code, std::string(MCMcoApi::InvalidResponseCode));
}

TEST_CASE("mco api: the lobby's poll names the player's identity and reads its rooms as MCO's launcher does: an engine "
          "left out is the original game's, counts and flags sent as text")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(Success({{"peers", Json::array({Json::object(), Json::object(), Json::object()})},
                            {"rooms",
                             {{{"room_id", "r1"},
                               {"name", "Redux night"},
                               {"host", "HOST"},
                               {"engine", "redux"},
                               {"engine_build", "official-0.2.1.61"},
                               {"engine_content", "abc"},
                               {"player_count", "2"},
                               {"max_players", 4},
                               {"in_game", "1"},
                               {"locked", true},
                               {"admission_closed", 0}},
                              {{"room_id", "r2"}, {"name", "Gold"}, {"players", Json::array({Json::object()})}},
                              "not a room"}}}));
    MCMcoApi::MCPeer peer = Registered();

    const auto lobby = service.Lobby(peer);
    REQUIRE(lobby);
    CHECK_EQ(lobby->Players, size_t{3});
    REQUIRE_EQ(lobby->Rooms.size(), size_t{2});
    const MCMcoApi::MCRoom& redux = lobby->Rooms[0];
    CHECK_EQ(redux.RoomId, std::string("r1"));
    CHECK_EQ(redux.Name, std::string("Redux night"));
    CHECK_EQ(redux.Host, std::string("HOST"));
    CHECK_EQ(redux.Engine, std::string(MCMcoApi::ReduxEngine));
    CHECK_EQ(redux.EngineBuild, std::string("official-0.2.1.61"));
    CHECK_EQ(redux.EngineContent, std::string("abc"));
    CHECK_EQ(redux.Players, 2);
    CHECK_EQ(redux.MaxPlayers, 4);
    CHECK(redux.InGame);
    CHECK(redux.Locked);
    CHECK(!redux.AdmissionClosed);
    const MCMcoApi::MCRoom& gold = lobby->Rooms[1];
    CHECK_EQ(gold.Engine, std::string("gold"));
    CHECK_EQ(gold.Players, 1);
    CHECK_EQ(gold.MaxPlayers, 6);
    CHECK(!gold.InGame);

    REQUIRE_EQ(mco.Asked().size(), size_t{1});
    const std::string& body = mco.Asked()[0].Post.Body;
    CHECK_EQ(Field(body, "action"), std::optional<std::string>("lobby_poll"));
    CHECK_EQ(Field(body, "peer_token"), std::optional<std::string>(PeerToken));
    CHECK_EQ(Field(body, "account_token"), std::optional<std::string>(AccountToken));
    CHECK_EQ(Field(body, "identity_ids"), std::optional<std::string>(ClientId));
    CHECK_EQ(Field(body, "is_ready"), std::optional<std::string>("0"));
    CHECK(!Field(body, "client_os").has_value());
}

TEST_CASE("mco api: a lobby poll without rooms asks get_rooms for them")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(Success());
    mco.AnswerJson(Success({{"rooms", {{{"room_id", "r9"}, {"name", "Late"}}}}}));
    MCMcoApi::MCPeer peer = Registered();

    const auto lobby = service.Lobby(peer);
    REQUIRE(lobby);
    REQUIRE_EQ(lobby->Rooms.size(), size_t{1});
    CHECK_EQ(lobby->Rooms[0].RoomId, std::string("r9"));
    REQUIRE_EQ(mco.Asked().size(), size_t{2});
    CHECK_EQ(Field(mco.Asked()[1].Post.Body, "action"), std::optional<std::string>("get_rooms"));
}

TEST_CASE("mco api: a lapsed peer token is renewed (recover_peer, saying the system) and the request asked again "
          "with the new one; leaving the lobby names no identities")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson({{"success", false}, {"error", "Peer session expired; rejoin the lobby."}}, HttpForbidden);
    mco.AnswerJson(Success({{"peer", {{"client_id", ClientId}}}, {"peer_token", NewPeerToken}}));
    mco.AnswerJson(Success({{"stock_pilot_build", "official-0.2.1.61"}}));
    mco.AnswerJson(Success());
    MCMcoApi::MCPeer peer = Registered();

    const auto build = service.StockBuild(peer);
    REQUIRE(build);
    CHECK_EQ(*build, std::optional<std::string>("official-0.2.1.61"));
    CHECK_EQ(peer.PeerToken, NewPeerToken);
    REQUIRE(service.Unregister(peer));

    REQUIRE_EQ(mco.Asked().size(), size_t{4});
    const std::string& recover = mco.Asked()[1].Post.Body;
    CHECK_EQ(Field(recover, "action"), std::optional<std::string>("recover_peer"));
    CHECK_EQ(Field(recover, "peer_token"), std::optional<std::string>(PeerToken));
    CHECK_EQ(Field(recover, "client_os"), std::optional<std::string>(std::string(TestOs)));
    CHECK(!Field(recover, "identity_ids").has_value());
    const std::string& again = mco.Asked()[2].Post.Body;
    CHECK_EQ(Field(again, "action"), std::optional<std::string>("redux_stock_status"));
    CHECK_EQ(Field(again, "peer_token"), std::optional<std::string>(NewPeerToken));
    const std::string& left = mco.Asked()[3].Post.Body;
    CHECK_EQ(Field(left, "action"), std::optional<std::string>("unregister_peer"));
    CHECK(!Field(left, "identity_ids").has_value());
}

TEST_CASE("mco api: a peer token renewed for another player is refused, and nothing is asked again")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson({{"success", false}, {"code", "peer_expired"}, {"error", "Expired."}}, HttpForbidden);
    mco.AnswerJson(Success({{"peer", {{"client_id", Hex('f', 32)}}}, {"peer_token", NewPeerToken}}));
    MCMcoApi::MCPeer peer = Registered();

    const auto build = service.StockBuild(peer);
    REQUIRE(!build);
    CHECK_EQ(build.error().Code, std::string("identity_changed"));
    CHECK_EQ(peer.PeerToken, PeerToken);
    CHECK_EQ(mco.Asked().size(), size_t{2});
}

TEST_CASE("mco api: signing in posts JSON to the account service, its account held to MCO's forms; activating asks "
          "first without taking the account from another computer")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(LoginAnswer());
    mco.AnswerJson({{"success", false}, {"code", "active_device"}, {"error", "Active elsewhere."}}, HttpForbidden);
    mco.AnswerJson(Success({{"peer_token", NewPeerToken}}));

    const auto signIn = service.Login("pilot@example.com", "secret pass");
    REQUIRE(signIn);
    CHECK_EQ(signIn->AccountToken, AccountToken);
    CHECK_EQ(signIn->ClientId, ClientId);
    CHECK_EQ(signIn->Username, std::string("PILOT"));
    CHECK_EQ(signIn->PlayerId, int64_t{42});

    const auto elsewhere = service.Activate(signIn->AccountToken, false);
    REQUIRE(!elsewhere);
    CHECK_EQ(elsewhere.error().Code, std::string(MCMcoApi::ActiveDeviceCode));
    CHECK_EQ(elsewhere.error().Status, HttpForbidden);
    const auto taken = service.Activate(signIn->AccountToken, true);
    REQUIRE(taken);
    CHECK_EQ(*taken, NewPeerToken);

    REQUIRE_EQ(mco.Asked().size(), size_t{3});
    const auto& login = mco.Asked()[0];
    CHECK_EQ(login.Url, std::string(AccountUrl));
    CHECK_EQ(login.Post.ContentType, std::string(JsonType));
    CHECK_EQ(login.Post.UserAgent, std::string(TestAgent));
    const Json loginBody = Json::parse(login.Post.Body);
    CHECK(loginBody == Json({{"action", "login"},
                             {"device_id", DeviceId},
                             {"email", "pilot@example.com"},
                             {"password", "secret pass"}}));
    CHECK(Json::parse(mco.Asked()[1].Post.Body)["take_over"] == false);
    CHECK(Json::parse(mco.Asked()[2].Post.Body)["take_over"] == true);
}

TEST_CASE("mco api: an account MCO's launcher wouldn't take is refused: a short token, a client id not hex, no expiry")
{
    const auto refused = [](const Json& answer)
    {
        MCFakeMco mco;
        MCMcoApi::MCService service(mco, TestClient());
        mco.AnswerJson(answer);
        const auto signIn = service.Login("pilot@example.com", "pass");
        return !signIn && signIn.error().Code == MCMcoApi::InvalidResponseCode;
    };

    Json shortToken = LoginAnswer();
    shortToken["account_token"] = Hex('a', 63);
    CHECK(refused(shortToken));
    Json notHex = LoginAnswer();
    notHex["account"]["client_id"] = Hex('z', 32);
    CHECK(refused(notHex));
    Json noExpiry = LoginAnswer();
    noExpiry.erase("expires_at");
    CHECK(refused(noExpiry));
    Json noAccount = LoginAnswer();
    noAccount.erase("account");
    CHECK(refused(noAccount));
}

TEST_CASE("mco api: making an account sends the guest's profile with the account's name, email and password, and "
          "creation_status's answer says whether one can be made")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(Success({{"available", false}}));
    mco.AnswerJson(LoginAnswer());
    MCMcoApi::MCPeer guest{.ClientId = ClientId, .PeerToken = PeerToken, .Username = "PILOT"};

    const auto can = service.CanCreate();
    REQUIRE(can);
    CHECK(!*can);
    const auto made = service.Create(guest, "PILOT", "pilot@example.com", "pass");
    REQUIRE(made);
    CHECK_EQ(made->ClientId, ClientId);

    REQUIRE_EQ(mco.Asked().size(), size_t{2});
    CHECK(Json::parse(mco.Asked()[0].Post.Body) == Json({{"action", "creation_status"}, {"device_id", DeviceId}}));
    const Json create = Json::parse(mco.Asked()[1].Post.Body);
    CHECK(create == Json({{"action", "create"},
                          {"device_id", DeviceId},
                          {"client_id", ClientId},
                          {"peer_token", PeerToken},
                          {"account_token", ""},
                          {"username", "PILOT"},
                          {"email", "pilot@example.com"},
                          {"password", "pass"}}));
}

TEST_CASE("mco api: MCO's refusals keep its text, code, status and wait; an answer not JSON, not an object, too "
          "long or never reached fails with ours")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson({{"success", false}, {"error", "Slow down."}, {"code", "rate_limited"}, {"retry_after", 30}},
                   HttpTooMany);
    mco.Answer(HttpOk, "<html>maintenance</html>");
    mco.Answer(HttpOk, "[1, 2]");
    mco.Answer(HttpOk, std::string(MCMcoApi::ReplyMost + 1, ' '));
    mco.Unreachable();
    mco.AnswerJson({{"success", true}}, HttpForbidden);
    MCMcoApi::MCPeer peer = Registered();

    const auto limited = service.StockBuild(peer);
    REQUIRE(!limited);
    CHECK_EQ(limited.error().Message, std::string("Slow down."));
    CHECK_EQ(limited.error().Code, std::string("rate_limited"));
    CHECK_EQ(limited.error().Status, HttpTooMany);
    CHECK_EQ(limited.error().RetryAfter, 30);

    const auto html = service.StockBuild(peer);
    REQUIRE(!html);
    CHECK_EQ(html.error().Code, std::string(MCMcoApi::InvalidResponseCode));
    const auto array = service.StockBuild(peer);
    REQUIRE(!array);
    CHECK_EQ(array.error().Code, std::string(MCMcoApi::InvalidResponseCode));
    const auto tooLong = service.StockBuild(peer);
    REQUIRE(!tooLong);
    CHECK_EQ(tooLong.error().Code, std::string("response_too_large"));
    const auto unreached = service.StockBuild(peer);
    REQUIRE(!unreached);
    CHECK(unreached.error().Unreachable);
    // A success with a failing status is a failure, as the launcher reads it.
    const auto forbidden = service.StockBuild(peer);
    REQUIRE(!forbidden);
    CHECK_EQ(forbidden.error().Status, HttpForbidden);
}

TEST_CASE("mco api: a form encodes as .NET's FormUrlEncodedContent, a player's name is held to MCO's rule, and our "
          "device id is never MCO's launcher's for the same seed")
{
    CHECK_EQ(MCMcoApi::FormBody({{"a b", "x&y=z"}, {"name", "Pilóto ~_.-"}}),
             std::string("a+b=x%26y%3Dz&name=Pil%C3%B3to+~_.-"));

    CHECK_EQ(MCMcoApi::PlayerName("  ACE  ").value_or(""), std::string("ACE"));
    CHECK_EQ(MCMcoApi::PlayerName("Pilóto測試").value_or(""), std::string("Pilóto測試"));
    CHECK(MCMcoApi::PlayerName("SIXTEEN CHARS 16").has_value());
    CHECK(!MCMcoApi::PlayerName("SEVENTEEN CHARS17").has_value());
    CHECK(!MCMcoApi::PlayerName("   ").has_value());
    CHECK(!MCMcoApi::PlayerName("BAD\x1b[2J").has_value());
    CHECK(!MCMcoApi::PlayerName("\xff\xfe").has_value());

    const std::string seed = "0123456789abcdef0123456789abcdef";
    const std::string ours = MCMcoApi::DeviceId(seed);
    CHECK(MCMcoApi::IsHex(ours, MCMcoApi::TokenDigits));
    CHECK_EQ(ours, MCMcoApi::DeviceId(seed));
    CHECK(ours != MCSha256::Hex(MCSha256::Of("MechCommanderOnline.Device.v1:" + seed)));
    CHECK(MCMcoApi::IsHex(MCMcoApi::NewClientId(), MCMcoApi::ClientIdDigits));
    CHECK(MCMcoApi::NewClientId() != MCMcoApi::NewClientId());
}

namespace
{
    /// <summary>A request's wait, and the server's step between tries.</summary>
    constexpr auto RequestTime = 5s;
    constexpr auto PollTime = 1ms;

    /// <summary>
    /// A server on this machine for the platform's POST: takes the requests that come, one connection at a time, each
    /// read whole (its head, then its Content-Length's body) and kept, and answers each with the next scripted answer.
    /// </summary>
    class MCPostServer
    {
    public:
        explicit MCPostServer(std::vector<std::string> answers) : _Answers(std::move(answers))
        {
            REQUIRE(_Net.Startup());
            _Listener = _Net.ListenTcp(0);
            REQUIRE(_Listener != MCSocket::InvalidHandle);
            _Port = _Net.LocalPort(_Listener);
            _Thread = std::thread([this] { Serve(); });
        }

        MCPostServer(const MCPostServer&) = delete;
        MCPostServer& operator=(const MCPostServer&) = delete;

        ~MCPostServer()
        {
            _Stop = true;
            _Thread.join();
            _Net.Close(_Listener);
        }

        std::string Address(std::string_view path) const { return std::format("http://127.0.0.1:{}{}", _Port, path); }

        /// <summary>Each request as it came (its head and body).</summary>
        std::vector<std::string> Requests()
        {
            const std::scoped_lock lock(_Lock);
            return _Requests;
        }

    private:
        void Serve()
        {
            size_t answered = 0;

            while (!_Stop)
            {
                MCSocket::Address from;
                const MCSocket::Handle client = _Net.AcceptTcp(_Listener, from);

                if (client == MCSocket::InvalidHandle)
                {
                    std::this_thread::sleep_for(PollTime);
                    continue;
                }

                if (std::string request = Read(client); !request.empty())
                {
                    {
                        const std::scoped_lock lock(_Lock);
                        _Requests.push_back(std::move(request));
                    }

                    if (answered < _Answers.size())
                    {
                        Send(client, _Answers[answered++]);
                    }
                }

                _Net.Close(client);
            }
        }

        /// <summary>A request whole, or empty when it doesn't come in time.</summary>
        std::string Read(MCSocket::Handle client)
        {
            constexpr std::string_view HeadEnd = "\r\n\r\n";
            constexpr std::string_view LengthField = "content-length: ";
            std::string request;
            std::optional<size_t> whole;
            const auto deadline = std::chrono::steady_clock::now() + RequestTime;

            while (!whole || request.size() < *whole)
            {
                std::array<uint8_t, 4096> buffer{};
                const int got = _Net.Receive(client, buffer);

                if (got < 0 || _Stop || std::chrono::steady_clock::now() > deadline)
                {
                    return {};
                }

                if (got == 0)
                {
                    std::this_thread::sleep_for(PollTime);
                    continue;
                }

                request.append(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(got));
                const size_t headEnd = request.find(HeadEnd);

                if (!whole && headEnd != std::string::npos)
                {
                    std::string head = request.substr(0, headEnd);
                    std::ranges::transform(head, head.begin(), [](char c)
                                           { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
                    const size_t at = head.find(LengthField);
                    const size_t length =
                        at != std::string::npos ? std::stoul(head.substr(at + LengthField.size())) : 0;
                    whole = headEnd + HeadEnd.size() + length;
                }
            }

            return request;
        }

        void Send(MCSocket::Handle client, std::string_view data)
        {
            const auto deadline = std::chrono::steady_clock::now() + RequestTime;

            while (!data.empty() && !_Stop && std::chrono::steady_clock::now() < deadline)
            {
                const int sent = _Net.Send(client, {reinterpret_cast<const uint8_t*>(data.data()), data.size()});

                if (sent < 0)
                {
                    return;
                }

                if (sent == 0)
                {
                    std::this_thread::sleep_for(PollTime);
                }

                data.remove_prefix(static_cast<size_t>(sent));
            }
        }

        MCSocketTransport _Net;
        std::vector<std::string> _Answers;
        MCSocket::Handle _Listener = MCSocket::InvalidHandle;
        uint16_t _Port = 0;
        std::atomic<bool> _Stop = false;
        std::mutex _Lock;
        std::vector<std::string> _Requests;
        std::thread _Thread;
    };

    /// <summary>Gathers an answer's status and body.</summary>
    class MCGathered final : public MCHttpSource::MCReply
    {
    public:
        bool Status(int status, std::string_view /*contentRange*/) override
        {
            Code = status;
            return true;
        }

        bool Body(std::span<const char> bytes) override
        {
            Text.append(bytes.data(), bytes.size());
            return true;
        }

        int Code = 0;
        std::string Text;
    };

    std::string Lowered(std::string text)
    {
        std::ranges::transform(text, text.begin(),
                               [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        return text;
    }
}

TEST_CASE("http: the platform's POST sends its body with its type, the user agent asked and asks for JSON, and a "
          "redirect is its answer, never followed")
{
    constexpr std::string_view Answer = R"({"success":true})";
    MCPostServer server(
        {std::format("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\n"
                     "Connection: close\r\n\r\n{}",
                     Answer.size(), Answer),
         "HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"});
    const auto transfer = MCMakeHttpGet();
    const MCHttpSource::MCPost post{.ContentType = std::string(FormType),
                                    .Body = "action=login&password=a+b%26c",
                                    .UserAgent = std::string(TestAgent)};

    MCGathered first;
    REQUIRE(transfer->Post(server.Address("/chat_api.php"), post, first));
    CHECK_EQ(first.Code, HttpOk);
    CHECK_EQ(first.Text, std::string(Answer));

    MCGathered moved;
    REQUIRE(transfer->Post(server.Address("/account_api.php"), post, moved));
    CHECK_EQ(moved.Code, HttpFound);

    const std::vector<std::string> requests = server.Requests();
    REQUIRE_EQ(requests.size(), size_t{2});
    const std::string head = Lowered(requests[0].substr(0, requests[0].find("\r\n\r\n")));
    const MCTest::Scope scope("the request: " + requests[0]);
    CHECK(requests[0].starts_with("POST /chat_api.php HTTP/1.1\r\n"));
    CHECK(head.contains(std::format("\r\ncontent-type: {}", FormType)));
    CHECK(head.contains(std::format("\r\nuser-agent: {}", Lowered(std::string(TestAgent)))));
    CHECK(head.contains(std::format("\r\naccept: {}", JsonType)));
    CHECK(requests[0].ends_with("\r\n\r\naction=login&password=a+b%26c"));
    CHECK(requests[1].starts_with("POST /account_api.php HTTP/1.1\r\n"));
}

TEST_CASE("mco api: the lobby's polls carry the chat's cursors on from each answer, as MCO's launcher does, and a new "
          "revision of the chat starts its messages over")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(Success({{"rooms", Json::array()},
                            {"message_revision", 5},
                            {"last_id", 10},
                            {"messages", {{{"id", 12}}, {{"id", 11}}}}}));
    mco.AnswerJson(Success({{"rooms", Json::array()}, {"message_revision", 6}, {"last_id", 3}}));
    mco.AnswerJson(Success({{"rooms", Json::array()}}));
    MCMcoApi::MCPeer peer = Registered();

    REQUIRE(service.Lobby(peer));
    REQUIRE(service.Lobby(peer));
    REQUIRE(service.Lobby(peer));

    REQUIRE_EQ(mco.Asked().size(), size_t{3});
    CHECK_EQ(Field(mco.Asked()[0].Post.Body, "last_id"), std::optional<std::string>("0"));
    CHECK_EQ(Field(mco.Asked()[0].Post.Body, "message_revision"), std::optional<std::string>("0"));
    CHECK_EQ(Field(mco.Asked()[1].Post.Body, "last_id"), std::optional<std::string>("12"));
    CHECK_EQ(Field(mco.Asked()[1].Post.Body, "message_revision"), std::optional<std::string>("5"));
    CHECK_EQ(Field(mco.Asked()[2].Post.Body, "last_id"), std::optional<std::string>("3"));
    CHECK_EQ(Field(mco.Asked()[2].Post.Body, "message_revision"), std::optional<std::string>("6"));
}

TEST_CASE("mco api: an account whose profile names no player is \"Player\", as the launcher names it, and MCO's "
          "client ids compare ignoring case")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    Json nameless = LoginAnswer();
    nameless.erase("profile");
    mco.AnswerJson(nameless);
    std::string upper = ClientId;
    std::ranges::transform(upper, upper.begin(), [](char c) { return static_cast<char>(std::toupper(c)); });
    mco.AnswerJson(Success({{"peer_token", NewPeerToken}, {"peer", {{"client_id", upper}}}}));

    const auto signIn = service.Login("pilot@example.com", "pass");
    REQUIRE(signIn);
    CHECK_EQ(signIn->Username, std::string("Player"));
    MCMcoApi::MCPeer peer{.ClientId = ClientId, .Username = signIn->Username};
    CHECK(service.Register(peer));
    CHECK(MCMcoApi::SameId("ABCDEF", "abcdef"));
    CHECK(!MCMcoApi::SameId("abcdef", "abcde0"));
}

TEST_CASE("mco api: MCO's text is shown at a terminal without its controls, C1's escapes, text-direction controls or "
          "bytes that aren't UTF-8")
{
    CHECK_EQ(MCMcoApi::Printable("Redux night"), std::string("Redux night"));
    CHECK_EQ(MCMcoApi::Printable("Pilóto 測試"), std::string("Pilóto 測試"));
    CHECK_EQ(MCMcoApi::Printable("A\x1b[31mB\x07\x7f"), std::string("A?[31mB??"));
    CHECK_EQ(MCMcoApi::Printable("C\xc2\x9b"
                                 "2J"),
             std::string("C?2J"));
    CHECK_EQ(MCMcoApi::Printable("\xe2\x80\xae"
                                 "evil"
                                 "\xe2\x81\xa6"),
             std::string("?evil?"));
    CHECK_EQ(MCMcoApi::Printable("bad\xff"), std::string("bad?"));
}

namespace
{
    using MCFormFields = std::vector<std::pair<std::string, std::string>>;

    const std::string RoomId = Hex('6', MCMcoApi::ClientIdDigits);
    const std::string SessionHex = Hex('5', MCMcoApi::ClientIdDigits);
    const std::string Content = Hex('9', 64);
    constexpr std::string_view StockBuild = "official-0.2.1.61";
    constexpr std::string_view RelayHost = "relay.mco.test";
    constexpr int RelayPort = 8443;
    constexpr int RelayTicketFewest = 64;

    /// <summary>The <paramref name="count"/> fields from <paramref name="first"/>'s on, or none when it isn't there.</summary>
    MCFormFields FieldsFrom(std::string_view body, std::string_view first, size_t count)
    {
        const MCFormFields fields = FormFields(body);
        const auto at = std::ranges::find_if(fields, [&](const auto& field) { return field.first == first; });

        if (at == fields.end() || static_cast<size_t>(fields.end() - at) < count)
        {
            return {};
        }

        return MCFormFields(at, at + static_cast<ptrdiff_t>(count));
    }

    MCMcoApi::MCRoom ReduxRoom()
    {
        return {.RoomId = RoomId,
                .Name = "Solaris Night",
                .Host = "HOST",
                .Engine = std::string(MCMcoApi::ReduxEngine),
                .EngineBuild = std::string(StockBuild),
                .EngineContent = Content,
                .Players = 2,
                .MaxPlayers = 4};
    }

    /// <summary>A match as MCO's create_session or a start_signal gives it.</summary>
    Json MatchJson()
    {
        return {{"engine_protocol", MCMcoApi::EngineProtocol},
                {"session_hex", SessionHex},
                {"room_id", RoomId},
                {"engine_build", StockBuild},
                {"engine_content", Content},
                {"stock_host", RelayHost},
                {"stock_port", RelayPort},
                {"capacity", 4}};
    }

    /// <summary>The relay's admission for <paramref name="role"/>, as MCO's redux_ticket gives it.</summary>
    Json AdmissionJson(std::string_view role)
    {
        return {{"protocol", MCMcoApi::EngineProtocol},
                {"host", RelayHost},
                {"port", RelayPort},
                {"peer", ClientId},
                {"room", SessionHex},
                {"room_id", RoomId},
                {"role", role},
                {"ticket", std::string(RelayTicketFewest, 'T')}};
    }

    MCMcoApi::MCMatch Match()
    {
        return {.SessionHex = SessionHex,
                .EngineBuild = std::string(StockBuild),
                .EngineContent = Content,
                .RoomId = RoomId,
                .RelayHost = std::string(RelayHost),
                .RelayPort = RelayPort,
                .Capacity = 4};
    }
}

TEST_CASE("mco api: joining a Redux room registers into it as MCO's launcher does: the room, READY, its id, then the "
          "engine it plays with our data")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(
        Success({{"peer_token", PeerToken}, {"peer", {{"client_id", ClientId}}}, {"room_last_message_id", 17}}));
    MCMcoApi::MCPeer peer = Registered();

    const auto seat = service.JoinRoom(peer, ReduxRoom(), "pass word", Content, true);
    REQUIRE(seat);
    CHECK_EQ(seat->Room, std::string("Solaris Night"));
    CHECK_EQ(seat->RoomId, RoomId);
    CHECK(!seat->Host);
    CHECK(seat->Ready);
    CHECK_EQ(seat->LastMessageId, int64_t{17});
    const std::string& body = mco.Asked().at(0).Post.Body;
    CHECK(Field(body, "action") == std::optional<std::string>("register_peer"));
    CHECK(FieldsFrom(body, "username", 11) == (MCFormFields{{"username", "PILOT"},
                                                            {"room", "Solaris Night"},
                                                            {"is_host", "0"},
                                                            {"max_players", "4"},
                                                            {"password", "pass word"},
                                                            {"is_ready", "1"},
                                                            {"room_id", RoomId},
                                                            {"engine", "redux"},
                                                            {"engine_protocol", "2"},
                                                            {"engine_build", std::string(StockBuild)},
                                                            {"engine_content", Content}}));
    CHECK(Field(body, "client_os").has_value());
}

TEST_CASE("mco api: hosting a Redux room registers it with its name, players and password, the stock build and our "
          "data, and keeps the room's id MCO gives")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    mco.AnswerJson(Success({{"peer_token", PeerToken}, {"peer", {{"client_id", ClientId}, {"room_id", RoomId}}}}));
    MCMcoApi::MCPeer peer = Registered();

    const auto seat = service.HostRoom(peer, "Night Raid", 3, "", StockBuild, Content);
    REQUIRE(seat);
    CHECK(seat->Host);
    CHECK(seat->Ready);
    CHECK_EQ(seat->RoomId, RoomId);
    CHECK(FieldsFrom(mco.Asked().at(0).Post.Body, "username", 11) ==
          (MCFormFields{{"username", "PILOT"},
                        {"room", "Night Raid"},
                        {"is_host", "1"},
                        {"max_players", "3"},
                        {"description", ""},
                        {"password", ""},
                        {"is_ready", "1"},
                        {"engine", "redux"},
                        {"engine_protocol", "2"},
                        {"engine_build", std::string(StockBuild)},
                        {"engine_content", Content}}));
}

TEST_CASE("mco api: a room's poll carries its cursor and, while a match runs, its session; it reads the players, the "
          "room's engine, the match started and the player taken out")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    MCMcoApi::MCPeer peer = Registered();
    MCMcoApi::MCSeat seat{.Room = "Solaris Night", .RoomId = RoomId, .Ready = true, .LastMessageId = 17};
    mco.AnswerJson(Success(
        {{"peers",
          {{{"client_id", Hex('7', 32)}, {"username", "HOST"}, {"is_ready", 1}, {"is_host", 1}},
           {{"client_id", ClientId}, {"username", "PILOT"}, {"is_ready", 0}, {"is_host", 0}}}},
         {"room_settings", {{"name", "Solaris Night"}, {"engine_build", StockBuild}, {"engine_content", Content}}},
         {"last_id", 25},
         {"messages", {{{"id", 30}}, {{"id", 31}}}},
         {"peer", {{"client_id", ClientId}, {"room_id", RoomId}, {"room", "Solaris Night"}}},
         {"start_signal", MatchJson()}}));

    const auto state = service.RoomPoll(peer, seat, SessionHex, true);
    REQUIRE(state);
    CHECK(FieldsFrom(mco.Asked().at(0).Post.Body, "room", 8) == (MCFormFields{{"room", "Solaris Night"},
                                                                              {"room_id", RoomId},
                                                                              {"last_id", "17"},
                                                                              {"limit", "50"},
                                                                              {"mention_cursor", "0"},
                                                                              {"is_ready", "1"},
                                                                              {"redux_process_session", SessionHex},
                                                                              {"redux_process_running", "1"}}));
    CHECK_EQ(seat.LastMessageId, int64_t{31});
    REQUIRE_EQ(state->Players.size(), size_t{2});
    CHECK(state->Players[0].Host);
    CHECK(state->Players[0].Ready);
    CHECK_EQ(state->Players[1].Username, std::string("PILOT"));
    CHECK(!state->Players[1].Ready);
    CHECK_EQ(state->EngineContent, Content);
    CHECK(!state->Gone);
    REQUIRE(state->Start.has_value());
    CHECK_EQ(state->Start->SessionHex, SessionHex);
    CHECK_EQ(state->Start->RelayHost, std::string(RelayHost));
    CHECK_EQ(state->Start->RelayPort, uint16_t{RelayPort});

    // No match running: no session asked about. Taken out of the room, or registered in another: gone.
    mco.AnswerJson(Success({{"removed", true}, {"exit_reason", {{"kind", "kicked"}}}}));
    const auto removed = service.RoomPoll(peer, seat);
    REQUIRE(removed);
    CHECK(!Field(mco.Asked().at(1).Post.Body, "redux_process_session").has_value());
    CHECK(removed->Gone);
    CHECK_EQ(removed->GoneWhy, std::string("kicked"));
    mco.AnswerJson(Success({{"peer", {{"client_id", ClientId}, {"room_id", Hex('8', 32)}, {"room", "Elsewhere"}}}}));
    const auto elsewhere = service.RoomPoll(peer, seat);
    REQUIRE(elsewhere);
    CHECK(elsewhere->Gone);
}

TEST_CASE("mco api: START asks for the match with the room's engine and no countdown, held to what the launcher "
          "takes: protocol 2, hex session and room, a relay named by DNS on a port it allows")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    MCMcoApi::MCPeer peer = Registered();
    mco.AnswerJson(Success(MatchJson()));

    const auto match = service.CreateSession(peer, StockBuild, Content);
    REQUIRE(match);
    CHECK_EQ(match->SessionHex, SessionHex);
    CHECK_EQ(match->RoomId, RoomId);
    CHECK_EQ(match->EngineContent, Content);
    CHECK_EQ(match->Capacity, 4);
    CHECK(FieldsFrom(mco.Asked().at(0).Post.Body, "engine", 4) ==
          (MCFormFields{{"engine", "redux"},
                        {"engine_build", std::string(StockBuild)},
                        {"engine_content", Content},
                        {"launch_countdown", "0"}}));

    const std::vector<std::pair<std::string, Json>> wrong{
        {"engine_protocol", 1},        {"session_hex", Hex('5', 31)},      {"room_id", "no room"},
        {"stock_host", "203.0.113.5"}, {"stock_host", "localhost"},        {"stock_host", "relay..mco.test"},
        {"stock_host", ".mco.test"},   {"stock_host", "relay_1.mco.test"}, {"stock_port", 80},
        {"stock_port", 70000},
    };

    for (const auto& [key, value] : wrong)
    {
        MCTest::Scope scope(std::format("{} {}", key, value.dump()));
        Json answer = MatchJson();
        answer[key] = value;
        mco.AnswerJson(Success(answer));
        const auto refused = service.CreateSession(peer, StockBuild, Content);
        REQUIRE(!refused);
        CHECK_EQ(refused.error().Code, std::string("invalid_match"));
    }
}

TEST_CASE("mco api: the match's ticket is asked for its session, the room's engine and the relay's epoch, and held "
          "to this match, player and role as the launcher holds it")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    MCMcoApi::MCPeer peer = Registered();
    const std::string epoch = Hex('e', 32);
    mco.AnswerJson(Success({{"admission", AdmissionJson("join")}}));

    const auto ticket = service.Ticket(peer, Match(), epoch, false);
    REQUIRE(ticket);
    CHECK_EQ(ticket->Ticket, std::string(RelayTicketFewest, 'T'));
    CHECK_EQ(ticket->Role, std::string("join"));
    const std::string& body = mco.Asked().at(0).Post.Body;
    CHECK(Field(body, "action") == std::optional<std::string>("redux_ticket"));
    CHECK(FieldsFrom(body, "session_hex", 4) == (MCFormFields{{"session_hex", SessionHex},
                                                              {"engine_build", std::string(StockBuild)},
                                                              {"engine_content", Content},
                                                              {"relay_epoch", epoch}}));

    const std::vector<std::pair<std::string, Json>> wrong{
        {"role", "host"},
        {"protocol", 1},
        {"host", "relay.other.test"},
        {"port", 8444},
        {"peer", Hex('f', 32)},
        {"room", Hex('4', 32)},
        {"room_id", Hex('4', 32)},
        {"ticket", std::string(RelayTicketFewest - 1, 'T')},
        {"ticket", std::string(RelayTicketFewest, '+')},
    };

    for (const auto& [key, value] : wrong)
    {
        MCTest::Scope scope(std::format("{} {}", key, value.dump()));
        Json admission = AdmissionJson("join");
        admission[key] = value;
        mco.AnswerJson(Success({{"admission", admission}}));
        const auto refused = service.Ticket(peer, Match(), epoch, false);
        REQUIRE(!refused);
        CHECK_EQ(refused.error().Message, std::string("The relay's admission couldn't be checked."));
    }

    mco.AnswerJson(Success({{"admission", AdmissionJson("host")}}));
    CHECK(service.Ticket(peer, Match(), epoch, true));
}

TEST_CASE("mco api: READY is a heartbeat; a match is published and closed by its session, a played one with the "
          "room's engine; leaving a room registers in the lobby")
{
    MCFakeMco mco;
    MCMcoApi::MCService service(mco, TestClient());
    MCMcoApi::MCPeer peer = Registered();
    MCMcoApi::MCSeat seat{.Room = "Solaris Night", .RoomId = RoomId};

    mco.AnswerJson(Success());
    REQUIRE(service.SetReady(peer, seat, true));
    CHECK(seat.Ready);
    CHECK(Field(mco.Asked().at(0).Post.Body, "action") == std::optional<std::string>("heartbeat"));
    CHECK(Field(mco.Asked().at(0).Post.Body, "is_ready") == std::optional<std::string>("1"));

    mco.AnswerJson(Success());
    REQUIRE(service.PublishReady(peer, SessionHex));
    CHECK(Field(mco.Asked().at(1).Post.Body, "session_hex") == std::optional<std::string>(SessionHex));
    CHECK(!Field(mco.Asked().at(1).Post.Body, "engine").has_value());

    mco.AnswerJson(Success());
    REQUIRE(service.CloseSession(peer, SessionHex, StockBuild, Content));
    CHECK(FieldsFrom(mco.Asked().at(2).Post.Body, "session_hex", 4) ==
          (MCFormFields{{"session_hex", SessionHex},
                        {"engine", "redux"},
                        {"engine_build", std::string(StockBuild)},
                        {"engine_content", Content}}));
    mco.AnswerJson(Success());
    REQUIRE(service.CloseSession(peer, SessionHex));
    CHECK(!Field(mco.Asked().at(3).Post.Body, "engine").has_value());

    mco.AnswerJson(Success({{"peer_token", PeerToken}, {"peer", {{"client_id", ClientId}}}}));
    REQUIRE(service.LeaveRoom(peer));
    CHECK(Field(mco.Asked().at(4).Post.Body, "action") == std::optional<std::string>("register_peer"));
    CHECK(Field(mco.Asked().at(4).Post.Body, "room") == std::optional<std::string>("Lobby"));
}
