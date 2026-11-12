#include "stdafx.h"
#include "linkup/MCSessionManager.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "lib/MCFatal.h"
#include "lib/MCMsvcSort.h"
#include "platform/MCDirectPlay.h"

// The session manager's sends: plain, guaranteed (numbered per player and kept until verified), to a group or to the
// server, the messages queued until this machine has a player number, file announcements, latency and the server
// switch.

namespace
{
    /// <summary>The "removed from the game" message type.</summary>
    constexpr uint16_t PlayerRemovedType = static_cast<uint16_t>(MCLinkupMessageType::PlayerRemoved);

    /// <summary>Sets <paramref name="set"/> and clears <paramref name="clear"/> in <paramref name="message"/>'s header word.</summary>
    void ChangeHeaderFlags(std::span<uint8_t> message, uint16_t set, uint16_t clear)
    {
        SetLinkupHeader(message, static_cast<uint16_t>((LinkupHeaderOf(message) | set) & ~clear));
    }
}

void MCSessionManager::SendSystemInformation()
{
    MCFISystemInfoMessage msg;
    msg.Header = LinkupHeader(MCLinkupMessageType::SystemInfo, FIMSG_GUARANTEED);
    msg.TotalPhysicalMemory = MCPort::TotalPhysicalMemory();

    if (!IsHost)
    {
        SendMessageToServerGuaranteed(LinkupBytes(msg));
    }
    else
    {
        MyPlayer()->TotalPhysicalMemory = msg.TotalPhysicalMemory;
    }
}

int MCSessionManager::RemovePlayerFromGame(MCFidpPlayer& player)
{
    if (_RemovingPlayer || !player.HasPlayerNumber)
    {
        return -1;
    }

    _RemovingPlayer = true;
    // Port-only: said, as the original removed a player in silence.
    SDL_Log("MCSessionManager: player %u removed from the game: %d of its guaranteed messages unacknowledged",
            player.Id, player.VerifyCountDifference());
    MCFIValueMessage msg;
    msg.Header = LinkupHeader(MCLinkupMessageType::PlayerRemoved, FIMSG_GUARANTEED);
    msg.Value = player.Id;
    SendMessageToPlayerGuaranteed(player.Id, LinkupBytes(msg), true);
    player.HasPlayerNumber = false;
    _RemovingPlayer = false;
    return 0;
}

void MCSessionManager::SetupMessageSendCounts(std::span<uint8_t> message,
                                              std::span<const std::shared_ptr<MCFidpPlayer>> players) const
{
    for (const std::shared_ptr<MCFidpPlayer>& player : players)
    {
        // Port fix: a member the session no longer knows is listed as null; skipped.
        if (player != nullptr && !IsMyPlayer(*player) && player->HasPlayerNumber && player->PlayerNumber != -1 &&
            player->PlayerNumber < MaxLinkupPlayers)
        {
            player->OutgoingSendCount++;
            SetLinkupSendCount(message, player->PlayerNumber, player->OutgoingSendCount);
        }
    }

    ChangeHeaderFlags(message, FIMSG_GROUP_MESSAGE | FIMSG_GUARANTEED, 0);
}

void MCSessionManager::StartGame()
{
    MCFIGuaranteedMessageHeader msg;
    msg.Header = LinkupHeader(MCLinkupMessageType::GameStarted, FIMSG_GUARANTEED);
    SendMessageToGroup(0, LinkupBytes(msg));
    GameStarted = true;
}

void MCSessionManager::SendPing()
{
    // Port fix: cleared (the original sent the uninitialised rest of a stack buffer, its header included).
    MCFIPingMessage ping;
    ping.Header = LinkupHeader(MCLinkupMessageType::Ping);
    constexpr uint32_t pingHeaderSize = sizeof(MCFIGuaranteedMessageHeader) + 1;

    if (!IsHost)
    {
        SendMessageToGroup(0, LinkupBytes(ping, pingHeaderSize));
        return;
    }

    std::vector<int32_t> numbers;

    for (const auto& player : Players)
    {
        // Port fix: bounded to the six numbers the message holds.
        if (!IsMyPlayer(*player) && numbers.size() < MaxLinkupPlayers)
        {
            numbers.push_back(player->PlayerNumber);
        }
    }

    Assert(numbers.size() == Players.size() - 1, 0, "nPlayers is incorrect");
    MCMsvcSort(std::span(numbers),
               [this](const int32_t& number1, const int32_t& number2)
               {
                   const std::shared_ptr<MCFidpPlayer> player1 = GetPlayerNumber(number1);
                   const std::shared_ptr<MCFidpPlayer> player2 = GetPlayerNumber(number2);

                   if (player2->AverageLatency() < player1->AverageLatency())
                   {
                       return 1;
                   }

                   return player1->AverageLatency() < player2->AverageLatency() ? -1 : 0;
               });
    ping.Count = static_cast<uint8_t>(numbers.size());

    for (size_t i = 0; i < numbers.size(); i++)
    {
        ping.PlayerNumbers[i] = static_cast<uint8_t>(numbers[i]);
    }

    SendMessageToGroup(0, LinkupBytes(ping, ping.Count + pingHeaderSize));
}

void MCSessionManager::SendPlayersInGroupMessages(uint32_t playerID)
{
    for (const auto& group : Groups)
    {
        MCFIPlayersInGroupMessage msg;
        msg.Header = LinkupHeader(MCLinkupMessageType::PlayersInGroup);
        msg.GroupID = group->Id;

        // The message holds six members (all a session can have).
        for (size_t j = 0; j < group->Players.size() && j < msg.PlayerIDs.size(); j++)
        {
            msg.PlayerIDs[j] = group->Players[j];
        }

        if (playerID == 0)
        {
            SendMessageToGroup(0, LinkupBytes(msg));
        }
        else
        {
            SendMessageToPlayerGuaranteed(playerID, LinkupBytes(msg), true);
        }
    }
}

void MCSessionManager::SendPreIDGuaranteedMessages()
{
    const size_t numGroupMessages = PreIDGroupMessages.size();

    for (size_t i = 0; i < numGroupMessages && !PreIDGroupMessages.empty(); i++)
    {
        std::shared_ptr<MCFidpMessage> msg = PreIDGroupMessages.front();
        SendMessageFromInfo(*msg);
        AddMessageToEmptyQueue(std::move(msg));
        PreIDGroupMessages.pop_front();
    }

    const size_t numServerMessages = PreIDServerMessages.size();

    for (size_t i = 0; i < numServerMessages && !PreIDServerMessages.empty(); i++)
    {
        const std::shared_ptr<MCFidpMessage> msg = PreIDServerMessages.front();
        const std::span<uint8_t> message = msg->Bytes();

        // Original behaviour: these messages are not returned to the free queue.
        if ((LinkupHeaderOf(message) & FIMSG_GUARANTEED) == 0)
        {
            SendMessageToServer(message);
        }
        else
        {
            SendMessageToServerGuaranteed(message);
        }

        PreIDServerMessages.pop_front();
    }
}

void MCSessionManager::SendMessageToGroup(uint32_t groupID, std::span<uint8_t> message)
{
    if (!HasPlayerNumber)
    {
        if (!LaunchedFromLobby)
        {
            std::scoped_lock lock(CriticalSection);
            ChangeHeaderFlags(message, FIMSG_GROUP_MESSAGE, 0);
            PreIDGroupMessages.push_back(CopyToFreeMessage(message, groupID));
        }

        return;
    }

    const std::vector<std::shared_ptr<MCFidpPlayer>> list = groupID == 0 ? Players : GetPlayerListForGroup(groupID);

    if (!_SessionLocked || !LaunchedFromLobby)
    {
        for (const std::shared_ptr<MCFidpPlayer>& player : list)
        {
            // Port fix: a member the session no longer knows is listed as null; skipped.
            if (player != nullptr && !IsMyPlayer(*player))
            {
                SendMessageToPlayerGuaranteed(player->Id, message, true);
            }
        }

        return;
    }

    for (const std::shared_ptr<MCFidpPlayer>& player : list)
    {
        if (player != nullptr && !IsMyPlayer(*player) && player->IsVerifyListFull())
        {
            RemovePlayerFromGame(*player);
        }
    }

    std::scoped_lock lock(CriticalSection);
    SetupMessageSendCounts(message, list);

    if (SendPlainMessage(groupID, message) == 0)
    {
        for (const std::shared_ptr<MCFidpPlayer>& player : list)
        {
            if (player != nullptr && player->HasPlayerNumber && !IsMyPlayer(*player))
            {
                ChangeHeaderFlags(message, 0, FIMSG_GROUP_MESSAGE);
                player->AddToVerifyList(CopyToFreeMessage(message, player->Id));
            }
        }
    }
}

std::vector<std::shared_ptr<MCFidpPlayer>> MCSessionManager::GetPlayerListForGroup(uint32_t groupID)
{
    const std::shared_ptr<MCFidpGroup> group = GetGroup(groupID);
    Assert(group != nullptr, 0, "Group does not exist");
    std::vector<std::shared_ptr<MCFidpPlayer>> list;

    if (group != nullptr)
    {
        for (const uint32_t playerID : group->Players)
        {
            list.push_back(GetPlayer(playerID));
        }
    }

    return list;
}

uint32_t MCSessionManager::TallyLatencies()
{
    uint32_t total = 0;

    for (const auto& player : Players)
    {
        if (!IsMyPlayer(*player))
        {
            total += player->AverageLatency();
        }
    }

    if (Players.size() == 1)
    {
        return 0;
    }

    return total / static_cast<uint32_t>(Players.size() - 1);
}

void MCSessionManager::SendLatencyInfo()
{
    if (!IsHost)
    {
        MCFIValueMessage msg;
        msg.Value = TallyLatencies();
        msg.Header = LinkupHeader(MCLinkupMessageType::Latency, FIMSG_GUARANTEED);
        SendMessageToServerGuaranteed(LinkupBytes(msg));
        return;
    }

    const std::shared_ptr<MCFidpPlayer> me = MyPlayer();
    me->ReportedLatency = TallyLatencies();

    if (me->ReportedLatency == 0)
    {
        me->ReportedLatency = 1000;
    }
}

void MCSessionManager::SwitchServers()
{
    if (!IsHost || LaunchedFromLobby || Players.empty())
    {
        return;
    }

    uint32_t mostMemory = 100000;
    std::shared_ptr<MCFidpPlayer> best = Players.front();

    for (const auto& player : Players)
    {
        if (mostMemory < player->TotalPhysicalMemory)
        {
            best = player;
            mostMemory = player->TotalPhysicalMemory;
        }
    }

    if (!IsMyPlayer(*best))
    {
        ServerID = best->Id;
        IsHost = false;
        _ServerMessage.Value = ServerID;
        SendMessageToGroup(0, LinkupBytes(_ServerMessage));
    }
}

void MCSessionManager::SendMessageToPlayerGuaranteed(uint32_t playerID, std::span<uint8_t> message, bool firstSend)
{
    if (!HasPlayerNumber)
    {
        if (!LaunchedFromLobby)
        {
            std::scoped_lock lock(CriticalSection);
            ChangeHeaderFlags(message, 0, FIMSG_GROUP_MESSAGE);
            PreIDGroupMessages.push_back(CopyToFreeMessage(message, playerID));
        }

        return;
    }

    const std::shared_ptr<MCFidpPlayer> player = GetPlayer(playerID);

    if (player == nullptr || IsMyPlayer(*player))
    {
        return;
    }

    if ((LinkupHeaderOf(message) & FIMSG_TYPE_MASK) != PlayerRemovedType &&
        (!player->HasPlayerNumber || (player->IsVerifyListFull() && RemovePlayerFromGame(*player) == 0)))
    {
        return;
    }

    std::scoped_lock lock(CriticalSection);

    if (firstSend && player->PlayerNumber >= 0 && player->PlayerNumber < MaxLinkupPlayers)
    {
        player->OutgoingSendCount++;
        SetLinkupSendCount(message, player->PlayerNumber, player->OutgoingSendCount);
    }

    ChangeHeaderFlags(message, FIMSG_GUARANTEED, FIMSG_GROUP_MESSAGE);
    const int32_t result = SendPlainMessage(playerID, message);
    const std::shared_ptr<MCFidpMessage> msg = CopyToFreeMessage(message, playerID);
    player->AddToVerifyList(msg);

    if (result != 0)
    {
        // Original behaviour: a failed send is backdated so it is resent at once. Port fix: by the counter's ticks, which
        // the verify list keeps (the original took timeGetTime's milliseconds).
        msg->SendTime -= static_cast<uint64_t>(player->ResendDelay) * TicksPerMs();

        if (!msg->WasResent)
        {
            msg->FirstSendTime = msg->SendTime;
        }
    }
}

void MCSessionManager::SendMessageToServerGuaranteed(std::span<uint8_t> message)
{
    if (!HasPlayerNumber)
    {
        std::scoped_lock lock(CriticalSection);
        ChangeHeaderFlags(message, 0, FIMSG_GROUP_MESSAGE);
        PreIDServerMessages.push_back(CopyToFreeMessage(message, ServerID));
        return;
    }

    SendMessageToPlayerGuaranteed(ServerID, message, true);
}

void MCSessionManager::BroadcastMessage(std::span<const uint8_t> message)
{
    SendPlainMessage(0, message);
}

void MCSessionManager::SendMessageToServer(std::span<uint8_t> message)
{
    std::scoped_lock lock(CriticalSection);

    if (!HasPlayerNumber)
    {
        ChangeHeaderFlags(message, 0, FIMSG_GROUP_MESSAGE);
        PreIDServerMessages.push_back(CopyToFreeMessage(message, ServerID));
    }
    else if (ServerID != MyPlayerID)
    {
        SendPlainMessage(ServerID, message);
    }
}

int32_t MCSessionManager::SendPlainMessage(uint32_t toID, std::span<const uint8_t> message)
{
    std::scoped_lock lock(CriticalSection);
    return static_cast<int32_t>(
        DirectPlay->Send(MyPlayerID, toID, 0, message.first(std::min<size_t>(message.size(), 0x200))));
}

void MCSessionManager::SendMessageFromInfo(MCFidpMessage& msg)
{
    const std::span<uint8_t> message = msg.Bytes();
    const uint16_t header = LinkupHeaderOf(message);

    if (msg.ToID == 0)
    {
        SendMessageToGroup(0, message);
    }
    else if ((header & FIMSG_GROUP_MESSAGE) != 0)
    {
        SendMessageToGroup(msg.ToID, message);
    }
    else if ((header & FIMSG_GUARANTEED) == 0)
    {
        SendPlainMessage(msg.ToID, message);
    }
    else
    {
        SendMessageToPlayerGuaranteed(msg.ToID, message, true);
    }
}

std::optional<std::string> MCSessionManager::GetStats()
{
    if (CurrentSession() == nullptr)
    {
        return std::nullopt;
    }

    if (!IsHost)
    {
        const std::shared_ptr<MCFidpPlayer> server = GetPlayer(ServerID);

        if (server == nullptr)
        {
            return std::nullopt;
        }

        return std::format("Latency to server ({}) = {}", server->Name, server->LastLatency);
    }

    std::string stats = "Latencies -- ";

    for (const auto& player : Players)
    {
        if (!IsMyPlayer(*player))
        {
            stats += std::format("<{}: {:4}> ", player->Name, player->LastLatency);
        }
    }

    return stats;
}
