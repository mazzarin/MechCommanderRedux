#pragma once

#include "platform/MCIceTransport.h"

#include <functional>

/// <summary>
/// The service's signal passing, inside the process: endpoints (each a game's own view of the service) that host games
/// by name and reach them, every signal (and a link's bytes, its WebSocket path) passed at once and in order. Links start on the loopback only (the Mac's
/// firewall: IceLinkTests.cpp). Thread-safe.
/// </summary>
class MCFakeSignalBoard
{
public:
    MCFakeSignalBoard();

    /// <summary>
    /// An endpoint named <paramref name="name"/> (one game's, for its transport); one name, one inbox. Its links find
    /// their paths by <paramref name="settings"/>: the loopback's, unless a test says.
    /// </summary>
    std::shared_ptr<MCIceSignaling> Endpoint(std::string_view name,
                                             MCIceLink::Settings settings = {.BindAddress = "127.0.0.1"});

    /// <summary>Has <paramref name="tamper"/> change each signal a host sends its guests, as a service that lies would.</summary>
    void TamperToGuests(std::function<void(MCIceLink::Signal&)> tamper);

    /// <summary>The endpoint <paramref name="endpoint"/> hosts the game <paramref name="game"/>: its guests' signals go there.</summary>
    void Host(std::string_view game, std::string_view endpoint);

    /// <summary>Every endpoint says it holds <paramref name="bytes"/> unsent, as a service that stopped reading would.</summary>
    void HoldUnsent(size_t bytes);

private:
    struct Board;
    class Point;
    std::shared_ptr<Board> _Board;
};
