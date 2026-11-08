#pragma once

#include "lib/MCThreadKeeper.h"
#include "network/MCInternetGame.h"

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <mutex>

// The games service's side of the INTERNET screens' sockets and requests, scripted by a test (the service's protocol as
// tools/cloudflare/games/README.md in the workspace has it): the list's socket (what it says, what the test has it
// hear) and the count's request.
namespace MCServiceScript
{
    /// <summary>The list's sockets: what each says, and what the test has it hear next.</summary>
    struct ListScript
    {
        std::vector<std::string> Opened;
        std::vector<std::string> Said;
        std::vector<MCWebSocket::Message> Inbox;
        MCWebSocket::State State = MCWebSocket::State::Open;
        bool ClosedByGame = false;
        /// <summary>The game closed it as a drop (CloseAway).</summary>
        bool ClosedAway = false;

        void Send(const nlohmann::json& message) { Inbox.push_back({false, message.dump()}); }
    };

    class ScriptedSocket final : public MCWebSocket
    {
    public:
        explicit ScriptedSocket(ListScript& script) : _Script(script) {}

        void SendText(std::string_view text) override { _Script.Said.emplace_back(text); }
        void SendBinary(std::span<const uint8_t>) override {}
        std::vector<Message> Take() override { return std::exchange(_Script.Inbox, {}); }
        State GetState() const override { return _Script.State; }
        std::string WhyClosed() const override { return "closed (1006)"; }
        size_t Unsent() const override { return 0; }

        void Close() override
        {
            _Script.ClosedByGame = true;
            _Script.State = State::Closed;
        }

        void CloseAway() override
        {
            Close();
            _Script.ClosedAway = true;
        }

    private:
        ListScript& _Script;
    };

    /// <summary>A socket opener for the list's sockets, kept in <paramref name="script"/>.</summary>
    inline MCWebSocketOpener ListOpener(ListScript& script)
    {
        return [&script](const std::string& url) -> std::unique_ptr<MCWebSocket>
        {
            script.Opened.push_back(url);
            script.State = MCWebSocket::State::Open;
            script.ClosedByGame = false;
            script.ClosedAway = false;
            return std::make_unique<ScriptedSocket>(script);
        };
    }

    /// <summary>A stand-in transfer: answers with a body, or fails, or holds the request until it's cancelled.</summary>
    class StandInGet final : public MCHttpSource::MCGet
    {
    public:
        enum class Kind
        {
            Answer,
            Fail,
            Hold,
        };

        StandInGet(Kind kind, std::string body, std::string& asked) : _Kind(kind), _Body(std::move(body)), _Asked(asked)
        {
        }

        std::expected<void, MCHttpSource::MCGetError> Get(const std::string& url, uint64_t,
                                                          MCHttpSource::MCReply& reply) override
        {
            constexpr int HttpOk = 200;
            _Asked = url;

            if (_Kind == Kind::Fail)
            {
                return std::unexpected(MCHttpSource::MCGetError{.Unreachable = true, .Reason = "no connection"});
            }

            if (_Kind == Kind::Hold)
            {
                std::unique_lock lock(_Lock);
                _Cancelled.wait(lock, [&] { return _WasCancelled; });
                return std::unexpected(MCHttpSource::MCGetError{.Reason = "stopped"});
            }

            if (reply.Status(HttpOk, {}))
            {
                reply.Body(_Body);
            }

            return {};
        }

        void Cancel() override
        {
            const std::scoped_lock lock(_Lock);
            _WasCancelled = true;
            _Cancelled.notify_all();
        }

    private:
        Kind _Kind;
        std::string _Body;
        std::string& _Asked;
        std::mutex _Lock;
        std::condition_variable _Cancelled;
        bool _WasCancelled = false;
    };

    /// <summary>A transfer answering every request with <paramref name="body"/> (the URL asked kept in <paramref name="asked"/>).</summary>
    inline std::function<std::unique_ptr<MCHttpSource::MCGet>()> Answering(std::string body, std::string& asked)
    {
        return [body = std::move(body), &asked]
        { return std::make_unique<StandInGet>(StandInGet::Kind::Answer, body, asked); };
    }

    /// <summary>Internet games set up with <paramref name="open"/> and <paramref name="get"/> for the test's time.</summary>
    class SetupScope
    {
    public:
        SetupScope(MCWebSocketOpener open, std::function<std::unique_ptr<MCHttpSource::MCGet>()> get,
                   std::string_view service, std::string_view version)
        {
            MCInternetGame::Setup({.Open = std::move(open),
                                   .Get = std::move(get),
                                   .Version = std::string(version),
                                   .Copy = "a1b2c3d4e5f60718293a4b5c6d7e8f90",
                                   .Service = std::string(service)});
        }

        /// <summary>
        /// The setup gone, and the threads let go of meanwhile (a count's request) joined: what the stand-ins refer to
        /// (the URL asked) outlives them.
        /// </summary>
        ~SetupScope()
        {
            MCInternetGame::Setup({});
            MCThreadKeeper::Process().JoinAll();
        }

        SetupScope(const SetupScope&) = delete;
        SetupScope& operator=(const SetupScope&) = delete;
    };

    /// <summary>A listed game as the service sends it.</summary>
    inline nlohmann::json ListedGame(std::string_view code, std::string_view name, std::vector<std::string> players,
                                     int max, nlohmann::json battleSeconds, std::string_view version,
                                     std::string_view region = "EU", nlohmann::json mission = nullptr)
    {
        return nlohmann::json{
            {"code", code},       {"name", name},     {"players", players}, {"max", max},
            {"mission", mission}, {"region", region}, {"version", version}, {"battleSeconds", battleSeconds}};
    }
}
