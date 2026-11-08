#pragma once

#include "fakes/MCServiceScript.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string_view>

// Where the tests click on MULTIPLAYER ACCESS and INTERNET CONNECTION (the middles of their controls, in the screens'
// 640x480), INTERNET CONNECTION's elements (the LAN screen's), and the games a scripted list (fakes/MCServiceScript.h)
// shows: the internet screens' tests and the HD menus tour share them.
namespace MCInternetSpots
{
    /// <summary>MULTIPLAYER ACCESS's two buttons.</summary>
    constexpr int32_t InternetX = 477;
    constexpr int32_t InternetY = 166;
    constexpr int32_t LanX = 477;
    constexpr int32_t LanY = 228;

    /// <summary>INTERNET CONNECTION's HOST GAME, CANCEL, the host block's PUBLIC or PRIVATE, and the CODE box.</summary>
    constexpr int32_t HostX = 558;
    constexpr int32_t HostY = 325;
    constexpr int32_t CancelX = 580;
    constexpr int32_t CancelY = 116;
    constexpr int32_t PrivacyX = 390;
    constexpr int32_t PrivacyY = 335;
    constexpr int32_t CodeX = 494;
    constexpr int32_t CodeY = 315;

    /// <summary>INTERNET CONNECTION's elements (the LAN screen's).</summary>
    constexpr int32_t GameList = 2;
    constexpr int32_t PlayerList = 3;
    constexpr int32_t PlayerNameEntry = 4;
    constexpr int32_t JoinButton = 6;
    constexpr int32_t CreateButton = 9;
    constexpr int32_t GameNameEntry = 10;
    constexpr int32_t PlayersEntry = 11;

    /// <summary>The version the scripted games' game says it is, and a newer one.</summary>
    constexpr std::string_view Version = "0.6.0 (812)";
    constexpr std::string_view NewerVersion = "0.7.0 (900)";

    /// <summary>The service's list as it sends it: <paramref name="games"/>, and its counts.</summary>
    inline nlohmann::json Listing(nlohmann::json games, int online, int inGames)
    {
        return nlohmann::json{{"t", "list"}, {"online", online}, {"inGames", inGames}, {"games", std::move(games)}};
    }

    /// <summary>
    /// A list of every kind of game, open ones first: one with room (K7QF), a full one (PXYL); then one in battle
    /// (Q4RT); and one on a newer version (W8ZE).
    /// </summary>
    inline nlohmann::json EveryKindOfGame(int online, int inGames)
    {
        return Listing(
            {MCServiceScript::ListedGame("K7QF", "Bob's game", {"Bob", "Ann"}, 6, nullptr, Version),
             MCServiceScript::ListedGame("PXYL", "Full house", {"A", "B", "C", "D"}, 4, nullptr, Version, "NA"),
             MCServiceScript::ListedGame("Q4RT", "Night raid", {"Cat", "Dan"}, 4, 720, Version, "AS", "Lost Outpost"),
             MCServiceScript::ListedGame("W8ZE", "Next version", {"Eve"}, 6, nullptr, NewerVersion, "OC")},
            online, inGames);
    }
}
