#include "stdafx.h"
#include "network/MCRemove.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCMcoSeats.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRoomEnd.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCSocket.h"
#include <algorithm>
#include <charconv>
#include <format>
#include <map>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view RemovedFormat = "{} removed you from the game.";
    constexpr std::string_view KeptOutFormat = "You were removed from {}. Try again in {} {}.";
    constexpr std::string_view Minute = "minute";
    constexpr std::string_view Minutes = "minutes";
    /// <summary>
    /// The door's refusal: this, then the seconds left. A Redux HD build before REMOVE shows it as options it lacks
    /// ("Your game doesn't have this match's removed 480. Update your game to join."), which is still its way in.
    /// </summary>
    constexpr std::string_view DoorTag = "removed ";

    /// <summary>The host's door: each removed player's computer's address, and when it was removed.</summary>
    std::map<uint32_t, Clock::time_point> Door;
    std::function<Clock::time_point()> TestClock;

    Clock::time_point Now()
    {
        return TestClock ? TestClock() : Clock::now();
    }

    /// <summary>The whole seconds still to go of a removal at <paramref name="at"/>.</summary>
    std::chrono::seconds LeftOf(Clock::time_point at, Clock::time_point now)
    {
        return std::max(std::chrono::ceil<std::chrono::seconds>(MCRemove::KeptOutFor - (now - at)),
                        std::chrono::seconds::zero());
    }

    /// <summary>The host's game, in its room (never in a battle); none otherwise.</summary>
    std::shared_ptr<MCMultiPlayer> HostInRoom()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

        if (game == nullptr || game->SessionManager == nullptr || !game->SessionManager->IsHost ||
            game->SessionManager->DirectPlay == nullptr || game->InMission || game->PrepareScenarioReceived)
        {
            return nullptr;
        }

        return game;
    }
}

bool MCRemove::Player(uint32_t playerId)
{
    const std::shared_ptr<MCMultiPlayer> game = HostInRoom();

    if (game == nullptr || playerId == game->SessionManager->MyPlayerID)
    {
        return false;
    }

    MCSessionManager& manager = *game->SessionManager;
    const std::shared_ptr<MCFidpPlayer> player = manager.GetPlayer(playerId);
    const std::optional<uint32_t> ip = manager.DirectPlay->PlayerIp(playerId);

    if (player == nullptr || !ip.has_value())
    {
        SDL_Log("Remove: player %u isn't a guest's in this game", playerId);
        return false;
    }

    // Its game hears it first, ahead of the service's farewell (which may close the links it would come over).
    MCRoomEnd::TellRemoved(playerId);

    // The side it came from: our service's guest, or MCO's carried in (its seat below); else a computer at the door.
    if (const std::optional<std::string> guest =
            manager.Internet != nullptr ? manager.Internet->GuestAt(*ip) : std::nullopt;
        guest.has_value())
    {
        manager.Internet->Remove(*guest);
    }
    else if (!MCSocket::IsLinkIp(*ip))
    {
        Door[*ip] = Now();
    }

    // Its seat in MCO's room: a Redux HD guest's as it said, or MCO's player let in (named as its account).
    MCMcoSeats::KeepOut(playerId, player->Name);
    manager.DirectPlay->RemovePlayer(playerId, MCRoomEnd::LeaveWait);
    SDL_Log("Remove: %s removed from the game", player->Name.c_str());
    return true;
}

bool MCRemove::Waiting(std::string_view name)
{
    return HostInRoom() != nullptr && MCMcoSeats::KeepOut(std::nullopt, name);
}

std::optional<std::string> MCRemove::AtDoor(uint32_t ip)
{
    const Clock::time_point now = Now();
    std::erase_if(Door, [&](const auto& kept) { return now - kept.second >= KeptOutFor; });
    const auto found = Door.find(ip);

    if (found == Door.end())
    {
        return std::nullopt;
    }

    const std::chrono::seconds left = LeftOf(found->second, now);
    SDL_Log("Remove: a removed player's computer turned away at the door (%lld s still to go)",
            static_cast<long long>(left.count()));
    return DoorReason(left);
}

std::string MCRemove::RemovedLine(std::string_view host)
{
    return std::format(RemovedFormat, host);
}

std::string MCRemove::KeptOutLine(std::string_view game, std::chrono::seconds left)
{
    const int64_t minutes = std::max<int64_t>(std::chrono::ceil<std::chrono::minutes>(left).count(), 1);
    return std::format(KeptOutFormat, game, minutes, minutes == 1 ? Minute : Minutes);
}

std::string MCRemove::DoorReason(std::chrono::seconds left)
{
    return std::format("{}{}", DoorTag, left.count());
}

std::optional<std::chrono::seconds> MCRemove::DoorLeft(std::string_view reason)
{
    if (!reason.starts_with(DoorTag))
    {
        return std::nullopt;
    }

    const std::string_view number = reason.substr(DoorTag.size());
    int64_t seconds = 0;
    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), seconds);

    if (error != std::errc{} || end != number.data() + number.size() || seconds < 0)
    {
        return std::nullopt;
    }

    return std::chrono::seconds(seconds);
}

void MCRemove::SetClockForTests(std::function<Clock::time_point()> clock)
{
    TestClock = std::move(clock);
    Door.clear();
}
