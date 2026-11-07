#include "stdafx.h"
#include "MCFakeSignaling.h"

/// <summary>What every endpoint shares: who hosts which game, and what each endpoint has yet to take.</summary>
struct MCFakeSignalBoard::Board
{
    std::mutex Lock;
    std::map<std::string, std::string, std::less<>> Hosts;
    std::map<std::string, std::vector<MCIceSignaling::Heard>, std::less<>> Inboxes;
    std::function<void(MCIceLink::Signal&)> TamperToGuests;
};

/// <summary>One game's view of the service.</summary>
class MCFakeSignalBoard::Point final : public MCIceSignaling
{
public:
    Point(std::shared_ptr<Board> board, std::string_view name, MCIceLink::Settings settings)
        : _Board(std::move(board)), _Name(name), _Settings(std::move(settings))
    {
    }

    MCIceLink::Settings LinkSettings() override { return _Settings; }

    void ToHost(std::string_view game, uint32_t link, const MCIceLink::Signal& signal) override
    {
        const std::scoped_lock lock(_Board->Lock);
        const auto host = _Board->Hosts.find(game);

        // No such game: the service says so, as it would to a code that matches nothing.
        if (host == _Board->Hosts.end())
        {
            _Board->Inboxes[_Name].push_back({From::Host, std::string(game), link, std::nullopt});
            return;
        }

        _Board->Inboxes[host->second].push_back({From::Guest, _Name, link, signal});
    }

    void ToGuest(std::string_view guest, uint32_t link, const MCIceLink::Signal& signal) override
    {
        const std::scoped_lock lock(_Board->Lock);
        const auto hosted =
            std::ranges::find_if(_Board->Hosts, [&](const auto& entry) { return entry.second == _Name; });

        if (hosted != _Board->Hosts.end())
        {
            MCIceLink::Signal passed = signal;

            if (_Board->TamperToGuests)
            {
                _Board->TamperToGuests(passed);
            }

            _Board->Inboxes[std::string(guest)].push_back({From::Host, hosted->first, link, std::move(passed)});
        }
    }

    std::vector<Heard> Take() override
    {
        const std::scoped_lock lock(_Board->Lock);
        return std::exchange(_Board->Inboxes[_Name], {});
    }

private:
    std::shared_ptr<Board> _Board;
    std::string _Name;
    MCIceLink::Settings _Settings;
};

MCFakeSignalBoard::MCFakeSignalBoard() : _Board(std::make_shared<Board>())
{
}

std::shared_ptr<MCIceSignaling> MCFakeSignalBoard::Endpoint(std::string_view name, MCIceLink::Settings settings)
{
    return std::make_shared<Point>(_Board, name, std::move(settings));
}

void MCFakeSignalBoard::TamperToGuests(std::function<void(MCIceLink::Signal&)> tamper)
{
    const std::scoped_lock lock(_Board->Lock);
    _Board->TamperToGuests = std::move(tamper);
}

void MCFakeSignalBoard::Host(std::string_view game, std::string_view endpoint)
{
    const std::scoped_lock lock(_Board->Lock);
    _Board->Hosts.insert_or_assign(std::string(game), std::string(endpoint));
}
