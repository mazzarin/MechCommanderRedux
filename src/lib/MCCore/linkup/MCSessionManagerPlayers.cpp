#include "stdafx.h"
#include "linkup/MCSessionManager.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "lib/MCFatal.h"
#include "network/MCInternetGame.h"
#include "platform/MCDirectPlay.h"

// Players and groups coming and going (DirectPlay's system messages), the numbering the server hands out, and the
// linkup layer's own application messages.

bool MCSessionManager::ReadyToChooseServer() const
{
    return MyPlayer() != nullptr && _LatencyReportsIn;
}

bool MCSessionManager::SpeaksForSession(uint32_t fromID) const
{
    return fromID != 0 && (fromID == ServerID || (DirectPlay != nullptr && DirectPlay->IsSessionHostPlayer(fromID)));
}

bool MCSessionManager::MaySend(MCLinkupMessageType type, uint32_t fromID) const
{
    switch (type)
    {
        case MCLinkupMessageType::NewServer:
        {
            return fromID == ServerID;
        }

        case MCLinkupMessageType::PlayerNumbers:
        case MCLinkupMessageType::PlayersInGroup:
        case MCLinkupMessageType::GameStarted:
        case MCLinkupMessageType::PlayerRemoved:
        {
            return SpeaksForSession(fromID);
        }

        default:
        {
            return true;
        }
    }
}

void MCSessionManager::ProcessSystemInfoMessage(const MCFISystemInfoMessage& msg, uint32_t fromID)
{
    if (const std::shared_ptr<MCFidpPlayer> player = GetPlayer(fromID); player != nullptr)
    {
        player->TotalPhysicalMemory = msg.TotalPhysicalMemory;
    }
}

void MCSessionManager::ProcessLatencyMessage(const MCFIValueMessage& msg, uint32_t fromID)
{
    const std::shared_ptr<MCFidpPlayer> player = GetPlayer(fromID);
    Assert(player != nullptr, 0, "ProcessLatencyMessage - null player");

    if (player != nullptr)
    {
        player->ReportedLatency = msg.Value;
    }

    _LatencyReportsIn = std::ranges::none_of(Players, [](const auto& listed) { return listed->ReportedLatency == 0; });
}

void MCSessionManager::GivePlayerAnID(MCFidpPlayer& player)
{
    // numbers[0] is a sentinel; numbers[1..count] are the listed players' numbers, sorted.
    std::array<int32_t, MaxLinkupPlayers + 1> numbers;
    numbers.fill(30000);
    numbers[0] = 0;
    // Port fix: at most six numbers fit (the original would write past the array with more players listed).
    const size_t count = std::min<size_t>(Players.size(), MaxLinkupPlayers);

    for (size_t i = 0; i < count; i++)
    {
        numbers[i + 1] = Players[i]->PlayerNumber;
    }

    std::sort(numbers.begin() + 1, numbers.begin() + 1 + static_cast<ptrdiff_t>(count));
    int32_t number = numbers[count] + 1;

    for (size_t i = 1; i < count; i++)
    {
        if (numbers[i] + 1 < numbers[i + 1])
        {
            number = numbers[i] + 1;
            break;
        }
    }

    player.PlayerNumber = number;
    player.HasPlayerNumber = true;
}

void MCSessionManager::AddPlayerOrGroup(uint32_t playerType, uint32_t id, const DPNAME& name, uint32_t flags)
{
    if (playerType != DPPLAYERTYPE_PLAYER)
    {
        if (GetGroup(id) == nullptr)
        {
            Groups.push_back(std::make_shared<MCFidpGroup>(id, name, flags));
        }

        return;
    }

    if (GetPlayer(id) != nullptr)
    {
        return;
    }

    // Original behaviour: the numbers message below lists the players from the first one listed before this one,
    // so a player joining an empty list is sent no numbers.
    const bool listWasEmpty = Players.empty();
    const auto player = std::make_shared<MCFidpPlayer>(id, name, flags);

    if (HasPlayerNumber && (IsHost || !LaunchedFromLobby))
    {
        GivePlayerAnID(*player);

        // Port fix: a player past the six a session holds (only a host that breaks the rules can announce one) is
        // left out. The original ended the game ("Could not connect to game.").
        if (player->PlayerNumber < 0 || player->PlayerNumber >= MaxLinkupPlayers)
        {
            SDL_Log("MCSessionManager: left out player %u, numbered %d: a session holds %d players", id,
                    player->PlayerNumber, MaxLinkupPlayers);
            return;
        }
    }

    Players.push_back(player);

    if (!IsHost && LaunchedFromLobby)
    {
        player->HasPlayerNumber = false;
    }

    if (LaunchedFromLobby)
    {
        for (int32_t i = 0; i < MaxLinkupPlayers; i++)
        {
            if (_NewPlayerNumbers[i] == id)
            {
                player->PlayerNumber = i;
                player->HasPlayerNumber = true;
            }
        }
    }

    if (const std::shared_ptr<MCFidpSession> session = CurrentSession(); session != nullptr)
    {
        session->SessionDesc.dwCurrentPlayers = static_cast<uint32_t>(Players.size());
    }

    if (!IsHost)
    {
        return;
    }

    // The server tells the new player (or, in a lobby game, everyone) who has which number.
    MCFIPlayerNumbersMessage numbers;
    numbers.Header = LinkupHeader(MCLinkupMessageType::PlayerNumbers, FIMSG_GUARANTEED);

    if (!listWasEmpty)
    {
        for (const auto& listed : Players)
        {
            // Port fix: an unnumbered player is skipped (the original wrote it at index -1, into the send counters).
            if (listed->PlayerNumber >= 0 && listed->PlayerNumber < MaxLinkupPlayers)
            {
                numbers.PlayerIDs[listed->PlayerNumber] = listed->Id;
            }

            if (listed->Id == MyPlayerID)
            {
                numbers.ServerNumber = static_cast<uint8_t>(listed->PlayerNumber);
            }
        }
    }

    if (!LaunchedFromLobby)
    {
        SendMessageToPlayerGuaranteed(player->Id, LinkupBytes(numbers), true);
    }
    else
    {
        SendMessageToGroup(0, LinkupBytes(numbers));
    }

    SendPlayersInGroupMessages(player->Id);
}

void MCSessionManager::PlayerOrGroupLeaving(uint32_t playerType, uint32_t id, uint32_t flags)
{
    if (playerType != DPPLAYERTYPE_PLAYER)
    {
        return;
    }

    const std::shared_ptr<MCFidpPlayer> player = GetPlayer(id);

    if (player == nullptr)
    {
        return;
    }

    player->HasPlayerNumber = false;

    if (player->Id != ServerID)
    {
        return;
    }

    // The server left: the next player by the server's latency order takes over.
    std::shared_ptr<MCFidpPlayer> next;

    // Port-only (Redux HD): the session's hub left with it, and DirectPlay's stand-in rolled the session over to another
    // machine (MCDirectPlay.h): that machine's player is the next server on every machine, so the server and the hub
    // that numbers, admits and locks the session stay one machine.
    if ((flags & DPPLAYER_SERVERPLAYER) != 0 && DirectPlay != nullptr)
    {
        const auto hub = std::ranges::find_if(Players,
                                              [&](const std::shared_ptr<MCFidpPlayer>& candidate)
                                              {
                                                  return candidate->Id != id && candidate->HasPlayerNumber &&
                                                         DirectPlay->IsSessionHostPlayer(candidate->Id);
                                              });

        if (hub != Players.end())
        {
            next = *hub;
        }
    }

    // Port fix: bounded to the six numbers (the original ran on past the array when nobody was left).
    for (int32_t i = 0; next == nullptr && i < MaxLinkupPlayers; i++)
    {
        next = GetPlayerNumber(PlayersByLatency[i]);

        if (next != nullptr && !next->HasPlayerNumber)
        {
            next = nullptr;
        }
    }

    Assert(next != nullptr, 0, "No more players");

    if (next == nullptr)
    {
        return;
    }

    if (IsMyPlayer(*next))
    {
        IsHost = true;
    }

    ServerID = next->Id;
}

void MCSessionManager::DeletePlayerOrGroup(uint32_t playerType, uint32_t id)
{
    if (playerType == DPPLAYERTYPE_PLAYER)
    {
        const std::shared_ptr<MCFidpSession> session = CurrentSession();

        if (std::erase_if(Players, [&](const auto& player) { return player->Id == id; }) != 0 && session != nullptr)
        {
            session->SessionDesc.dwCurrentPlayers = static_cast<uint32_t>(Players.size());
        }

        return;
    }

    // The group goes too (the original took it off the list and kept it).
    if (const auto found = std::ranges::find_if(Groups, [&](const auto& group) { return group->Id == id; });
        found != Groups.end())
    {
        Groups.erase(found);
    }
}

void MCSessionManager::PlayerJoinedGroup(uint32_t groupID, uint32_t playerID)
{
    const std::shared_ptr<MCFidpGroup> group = GetGroup(groupID);

    if (group != nullptr && group->AddPlayer(playerID))
    {
        if (const std::shared_ptr<MCFidpPlayer> player = GetPlayer(playerID); player != nullptr)
        {
            player->JoinGroup(groupID);
        }
    }
}

void MCSessionManager::HandlePreSystemMessage(MCFidpMessage& msg)
{
    const std::span<const uint8_t> buffer = msg.Buffer();
    const uint32_t type = ReadLinkupMessage<DPMSG_GENERIC>(buffer).dwType;

    if (type == DPSYS_ADDPLAYERTOGROUP)
    {
        const auto added = ReadLinkupMessage<DPMSG_ADDPLAYERTOGROUP>(buffer);
        Assert(GetGroup(added.dpIdGroup) != nullptr, 0, "group is null");
        PlayerJoinedGroup(added.dpIdGroup, added.dpIdPlayer);
    }
    else if (type == DPSYS_CREATEPLAYERORGROUP)
    {
        const auto created = ReadLinkupMessage<DPMSG_CREATEPLAYERORGROUP>(buffer);
        AddPlayerOrGroup(created.dwPlayerType, created.dpId, MCDirectPlayMessageName(msg.Bytes(), created.dpnName),
                         created.dwFlags);
    }
    else if (type == DPSYS_DESTROYPLAYERORGROUP)
    {
        const auto destroyed = ReadLinkupMessage<DPMSG_DESTROYPLAYERORGROUP>(buffer);
        PlayerOrGroupLeaving(destroyed.dwPlayerType, destroyed.dpId, destroyed.dwFlags);
    }
    else if (type == DPSYS_HOST && Internet != nullptr)
    {
        // Port-only (Redux HD): the session rolled over to this machine (MCDirectPlay.h): its internet game is the
        // game's host on the service now, and the game's other guests reach it there.
        Internet->TakeOver(InternetRolloverWait);
    }
    else if (type == DPSYS_SETSESSIONDESC && LaunchedFromLobby)
    {
        if ((ReadLinkupMessage<DPMSG_SETSESSIONDESC>(buffer).dpDesc.dwFlags & DPSESSION_NEWPLAYERSDISABLED) != 0)
        {
            _SessionLocked = true;
        }
    }
}

void MCSessionManager::HandlePostSystemMessage(MCFidpMessage& msg)
{
    const std::span<const uint8_t> buffer = msg.Buffer();
    const uint32_t type = ReadLinkupMessage<DPMSG_GENERIC>(buffer).dwType;

    if (type == DPSYS_DESTROYPLAYERORGROUP)
    {
        const auto destroyed = ReadLinkupMessage<DPMSG_DESTROYPLAYERORGROUP>(buffer);
        DeletePlayerOrGroup(destroyed.dwPlayerType, destroyed.dpId);
    }
    else if (type == DPSYS_DELETEPLAYERFROMGROUP)
    {
        const auto removed = ReadLinkupMessage<DPMSG_ADDPLAYERTOGROUP>(buffer);
        const std::shared_ptr<MCFidpGroup> group = GetGroup(removed.dpIdGroup);

        // Port fix: a group that no longer exists is skipped (the original called through a null group).
        if (group != nullptr && group->RemovePlayer(removed.dpIdPlayer))
        {
            if (const std::shared_ptr<MCFidpPlayer> player = GetPlayer(removed.dpIdPlayer); player != nullptr)
            {
                player->LeaveGroup(removed.dpIdGroup);
            }
        }
    }
}

void MCSessionManager::ApplyLobbyPlayerNumbers(const MCFIPlayerNumbersMessage& numbers)
{
    for (int32_t i = 0; i < MaxLinkupPlayers; i++)
    {
        if (numbers.PlayerIDs[i] == 0)
        {
            continue;
        }

        if (const std::shared_ptr<MCFidpPlayer> player = GetPlayer(numbers.PlayerIDs[i]); player == nullptr)
        {
            _NewPlayerNumbers[i] = numbers.PlayerIDs[i];
        }
        else
        {
            player->PlayerNumber = i;
            player->HasPlayerNumber = true;
        }
    }
}

void MCSessionManager::HandleApplicationMessage(std::shared_ptr<MCFidpMessage> msg)
{
    bool passOn = true;
    const std::span<const uint8_t> bytes = msg->Bytes();
    const auto type = static_cast<MCLinkupMessageType>(msg->Type());

    // A refused message was still acknowledged, so its sender doesn't resend it.
    if (!MaySend(type, msg->FromID))
    {
        SDL_Log("MCSessionManager: dropped linkup message %u from player %u", static_cast<uint32_t>(type), msg->FromID);
        AddMessageToEmptyQueue(std::move(msg));
        return;
    }

    switch (type)
    {
        case MCLinkupMessageType::PlayerNumbers:
        {
            if (LaunchedFromLobby)
            {
                ApplyLobbyPlayerNumbers(ReadLinkupMessage<MCFIPlayerNumbersMessage>(bytes));
            }

            break;
        }

        case MCLinkupMessageType::PlayersInGroup:
        {
            const auto members = ReadLinkupMessage<MCFIPlayersInGroupMessage>(bytes);

            if (GetGroup(members.GroupID) != nullptr)
            {
                for (const uint32_t playerID : members.PlayerIDs)
                {
                    if (playerID == 0)
                    {
                        break;
                    }

                    PlayerJoinedGroup(members.GroupID, playerID);
                }
            }

            passOn = false;
            break;
        }

        case MCLinkupMessageType::GameStarted:
        {
            GameStarted = true;
            break;
        }

        case MCLinkupMessageType::NewServer:
        {
            ServerID = ReadLinkupMessage<MCFIValueMessage>(bytes).Value;
            IsHost = ServerID == MyPlayerID;
            break;
        }

        case MCLinkupMessageType::BeginFileTransfer:
        case MCLinkupMessageType::FileData:
        {
            // The game never sends files (nothing in MCX.EXE called MultiPlayer::sendFile), and receiving them let any
            // peer write files on this machine: file transfers are dropped.
            passOn = false;
            break;
        }

        case MCLinkupMessageType::Ping:
        {
            HandlePingUpdate(*msg);
            passOn = false;
            break;
        }

        case MCLinkupMessageType::SystemInfo:
        {
            ProcessSystemInfoMessage(ReadLinkupMessage<MCFISystemInfoMessage>(bytes), msg->FromID);
            passOn = false;
            break;
        }

        case MCLinkupMessageType::Latency:
        {
            ProcessLatencyMessage(ReadLinkupMessage<MCFIValueMessage>(bytes), msg->FromID);
            break;
        }

        default:
        {
            break;
        }
    }

    if (passOn && ApplicationCallback)
    {
        // Port: the original left the critical section around the callback (for its receive thread, which never
        // existed); the port has one thread, and its callers don't always hold the lock, so it is left alone.
        ApplicationCallback(*msg);
    }

    AddMessageToEmptyQueue(std::move(msg));
}

void MCSessionManager::HandlePingUpdate(const MCFidpMessage& msg)
{
    if (msg.FromID != ServerID)
    {
        // Port-only (the personal build): a guest's ping says its room's screen is up (MCFidpPlayer::HeardPing).
        if (const std::shared_ptr<MCFidpPlayer> player = GetPlayer(msg.FromID); player != nullptr && IsHost)
        {
            player->HeardPing = true;
        }

        return;
    }

    const auto ping = ReadLinkupMessage<MCFIPingMessage>(msg.Bytes());

    // Port fix: bounded to the six numbers the message holds.
    for (int32_t i = 0; i < ping.Count && i < MaxLinkupPlayers; i++)
    {
        PlayersByLatency[i] = ping.PlayerNumbers[i];
    }
}
