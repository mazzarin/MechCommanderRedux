#pragma once

#include "platform/MCIceTransport.h"

/// <summary>
/// The service's signal passing, inside the process: endpoints (each a game's own view of the service) that host games
/// by name and reach them, every signal passed at once and in order. Links start on the loopback only (the Mac's
/// firewall: IceLinkTests.cpp). Thread-safe.
/// </summary>
class MCFakeSignalBoard
{
public:
    MCFakeSignalBoard();

    /// <summary>An endpoint named <paramref name="name"/> (one game's, for its transport); one name, one inbox.</summary>
    std::shared_ptr<MCIceSignaling> Endpoint(std::string_view name);

    /// <summary>The endpoint <paramref name="endpoint"/> hosts the game <paramref name="game"/>: its guests' signals go there.</summary>
    void Host(std::string_view game, std::string_view endpoint);

private:
    struct Board;
    class Point;
    std::shared_ptr<Board> _Board;
};
