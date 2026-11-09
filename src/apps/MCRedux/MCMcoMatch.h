#pragma once

#include "network/MCMcoApi.h"
#include "platform/MCTlsLink.h"

#include <filesystem>
#include <string>

// Port-only (Redux HD): the MCO probe's rooms (MCMcoProbe.h): a registered player joins or hosts one of MechCommander
// Online's Redux rooms, and each match the room starts is played by a copy of the game on this computer, carried over
// MCO's relay by the bridge (network/MCMcoBridge.h), as MCO's launcher plays it with its helper.

namespace MCMcoMatch
{
    /// <summary>How the room is joined or hosted, and how its matches' games start.</summary>
    struct MCOptions
    {
        /// <summary>The room to join (none: chosen from the list), or the room's name to host (none: asked).</summary>
        std::string Room;
        /// <summary>A locked room's password (asked when needed), or the hosted room's (none: open).</summary>
        std::string Password;
        int32_t MaxPlayers = MCMcoApi::RoomMost;
        /// <summary>The game data: its fingerprint is the room's content; passed to the game when given.</summary>
        std::filesystem::path DataFolder;
        bool DataGiven = false;
        /// <summary>The save folder, passed to the game when given.</summary>
        std::filesystem::path UserFolder;
        bool UserGiven = false;
        /// <summary>The one authority the relay is trusted by (a lab's stand-in); none: the computer's trust.</summary>
        std::string RelayAnchor;
        /// <summary>Starts the game for each match; else says how, for the player to start it.</summary>
        bool Launch = true;
    };

    /// <summary>Joins a room as <paramref name="peer"/> (registered in the lobby) and plays its matches.</summary>
    /// <returns>The exit code: 0 once the player left the room.</returns>
    int Join(MCMcoApi::MCService& service, MCMcoApi::MCPeer& peer, const MCOptions& options);

    /// <summary>Hosts a room as <paramref name="peer"/> and plays its matches, started by the player's START.</summary>
    int Host(MCMcoApi::MCService& service, MCMcoApi::MCPeer& peer, const MCOptions& options);
}
