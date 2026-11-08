#pragma once

#include "platform/MCServiceSignaling.h"
#include "platform/MCWebSocket.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <mutex>

// A stand-in for the games service in the process, for one game (its protocol as tools/cloudflare/games/README.md in the
// workspace has it): hosting, joining by the code, the signals passed with each guest's tag, pings, the host's room kept,
// a guest's leaving told to its host and a host's to its guests. No relays (the links are on the loopback), no WebSocket
// path and no list (tests/fakes/MCServiceScript.h scripts the list).
namespace MCFakeGames
{
    using Json = nlohmann::json;

    /// <summary>The service's address, the code it gives the one game hosted on it, and that game's token.</summary>
    constexpr std::string_view ServiceUrl = "wss://games.test";
    constexpr std::string_view GameCode = "K7QF";
    constexpr std::string_view GameToken = "0123456789abcdef0123456789abcdef";

    class MCFakeGamesService;

    /// <summary>A socket to the stand-in service: what it sends goes to the service at once, what it hears waits.</summary>
    class MCServiceLine final : public MCWebSocket
    {
    public:
        MCServiceLine(MCFakeGamesService& service, int id) : _Service(service), _Id(id) {}

        void SendText(std::string_view text) override;
        void SendBinary(std::span<const uint8_t>) override {}
        std::vector<Message> Take() override;
        State GetState() const override;
        std::string WhyClosed() const override { return "closed (1000)"; }
        size_t Unsent() const override { return 0; }
        void Close() override;

    private:
        MCFakeGamesService& _Service;
        int _Id;
    };

    /// <summary>
    /// The games service's protocol for one game, in the process: hosting, joining by the code, the signals passed
    /// with each guest's tag, pings, the room kept, a guest's leaving told to its host and a host's to its guests.
    /// No relays (the links are on the loopback) and no WebSocket path.
    /// </summary>
    class MCFakeGamesService
    {
    public:
        /// <summary>What joined says of the game's being public: none, and it says nothing (an older service).</summary>
        std::optional<bool> JoinedPublic = false;
        /// <summary>The paths its sockets opened, in order.</summary>
        std::vector<std::string> Paths;

        MCWebSocketOpener Opener()
        {
            return [this](const std::string& url) -> std::unique_ptr<MCWebSocket>
            {
                const std::scoped_lock lock(_Lock);
                const int id = _NextId++;
                Line& line = _Lines[id];
                const std::string_view path = std::string_view(url).substr(ServiceUrl.size());
                Paths.emplace_back(path);
                Open(id, line, path);
                return std::make_unique<MCServiceLine>(*this, id);
            };
        }

        /// <summary>The host's room as it last said it.</summary>
        std::optional<Json> Room()
        {
            const std::scoped_lock lock(_Lock);
            return _Room;
        }

        /// <summary>Sockets open now.</summary>
        size_t OpenLines()
        {
            const std::scoped_lock lock(_Lock);
            return static_cast<size_t>(
                std::ranges::count_if(_Lines, [](const auto& line) { return line.second.Open; }));
        }

        void Heard(int id, std::string_view text)
        {
            const std::scoped_lock lock(_Lock);
            Line& from = _Lines.at(id);

            if (!from.Open)
            {
                return;
            }

            if (text == MCServicePing)
            {
                Say(from, MCServicePong);
                return;
            }

            const Json message = Json::parse(text);

            if (from.Host && message.at("t") == "room")
            {
                _Room = message;
            }
            else if (from.Host && message.at("t") == "signal")
            {
                Json signal = message;
                const std::string guest = signal.at("guest");
                signal.erase("guest");

                for (auto& [other, line] : _Lines)
                {
                    if (line.Open && line.Guest == guest)
                    {
                        Say(line, signal.dump());
                    }
                }
            }
            else if (!from.Host && message.at("t") == "signal")
            {
                Json signal = message;
                signal["guest"] = from.Guest;

                if (Line* const host = HostLine())
                {
                    Say(*host, signal.dump());
                }
            }
        }

        std::vector<MCWebSocket::Message> Take(int id)
        {
            const std::scoped_lock lock(_Lock);
            return std::exchange(_Lines.at(id).Inbox, {});
        }

        MCWebSocket::State StateOf(int id)
        {
            const std::scoped_lock lock(_Lock);
            return _Lines.at(id).Open ? MCWebSocket::State::Open : MCWebSocket::State::Closed;
        }

        void Closed(int id)
        {
            const std::scoped_lock lock(_Lock);
            Line& line = _Lines.at(id);

            if (!line.Open)
            {
                return;
            }

            line.Open = false;

            for (auto& [other, peer] : _Lines)
            {
                if (!peer.Open)
                {
                    continue;
                }

                if (line.Host)
                {
                    Say(peer, R"({"t":"gone"})");
                    peer.Open = false;
                }
                else if (peer.Host)
                {
                    Say(peer, Json{{"t", "left"}, {"guest", line.Guest}}.dump());
                }
            }
        }

    private:
        struct Line
        {
            bool Open = true;
            bool Host = false;
            std::string Guest;
            std::vector<MCWebSocket::Message> Inbox;
        };

        static void Say(Line& line, std::string_view text) { line.Inbox.push_back({false, std::string(text)}); }

        static Json NoRelays() { return Json{{"stun", Json::array()}, {"relays", Json::array()}}; }

        Line* HostLine()
        {
            const auto host =
                std::ranges::find_if(_Lines, [](const auto& line) { return line.second.Open && line.second.Host; });
            return host != _Lines.end() ? &host->second : nullptr;
        }

        void Open(int id, Line& line, std::string_view path)
        {
            if (path.starts_with("/host?"))
            {
                line.Host = true;
                Say(line, Json{{"t", "hosted"}, {"game", GameCode}, {"token", GameToken}, {"ice", NoRelays()}}.dump());
                return;
            }

            const std::string join = std::format("/join/{}?", GameCode);
            Line* const host = HostLine();

            if (!path.starts_with(join) || host == nullptr)
            {
                Say(line, R"({"t":"nogame"})");
                line.Open = false;
                return;
            }

            line.Guest = std::format("g{}", id);
            Say(*host, Json{{"t", "guest"}, {"guest", line.Guest}, {"ice", NoRelays()}}.dump());
            Json joined{
                {"t", "joined"}, {"game", GameCode}, {"guest", line.Guest}, {"token", GameToken}, {"ice", NoRelays()}};

            if (JoinedPublic.has_value())
            {
                joined["public"] = *JoinedPublic;
            }

            Say(line, joined.dump());
        }

        std::mutex _Lock;
        std::map<int, Line> _Lines;
        std::optional<Json> _Room;
        int _NextId = 1;
    };

    inline void MCServiceLine::SendText(std::string_view text)
    {
        _Service.Heard(_Id, text);
    }

    inline std::vector<MCWebSocket::Message> MCServiceLine::Take()
    {
        return _Service.Take(_Id);
    }

    inline MCWebSocket::State MCServiceLine::GetState() const
    {
        return _Service.StateOf(_Id);
    }

    inline void MCServiceLine::Close()
    {
        _Service.Closed(_Id);
    }
}
