#include "stdafx.h"
#include "linkup/MCSessionManager.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "lib/MCFatal.h"
#include "platform/MCDirectPlay.h"

// The session manager's per-frame pump: the receive pass that sorts what arrived into the queues, guaranteed delivery
// (verifies, in-order hand-over, resends) and the processing of the queues.

namespace
{
    /// <summary>Whether a received header word is a plain message, or the "removed" message (always handled at once).</summary>
    bool IsPlainOrRemoved(uint16_t header)
    {
        return (header & FIMSG_GUARANTEED) == 0 ||
               (header & FIMSG_TYPE_MASK) == static_cast<uint16_t>(MCLinkupMessageType::PlayerRemoved);
    }

    /// <summary>The type of the system message in <paramref name="msg"/>.</summary>
    uint32_t SystemTypeOf(const MCFidpMessage& msg)
    {
        return ReadLinkupMessage<DPMSG_GENERIC>(msg.Bytes()).dwType;
    }

    /// <summary>The "removed from the game" message type.</summary>
    constexpr uint16_t PlayerRemovedType = static_cast<uint16_t>(MCLinkupMessageType::PlayerRemoved);
}

void MCSessionManager::AddVerifyEntry(int32_t playerNumber, uint8_t sendCount)
{
    MCFIVerifyMessage& verify = _VerifyMessages[playerNumber];
    verify.Entries[verify.Count].Clear();
    verify.Entries[verify.Count].SendCount[playerNumber] = sendCount;
    verify.Count++;
}

void MCSessionManager::ProcessMessages()
{
    PumpClosed();

    if (MyPlayerID == 0)
    {
        return;
    }

    TendInternet();

    if (HasPlayerNumber && Players.size() > 1 && IsHost)
    {
        const auto now = static_cast<uint64_t>(MCPort::PerformanceCounter());

        // Port fix: the whole counter; its low 32 bits wrap every 4.3 seconds on a nanosecond counter.
        if (now > _NextPingTime)
        {
            SendPing();
            _NextPingTime = now + static_cast<uint64_t>(_PingInterval) * TicksPerMs();
        }
    }

    std::scoped_lock lock(CriticalSection);
    ReceiveThread();
    UpdateGuaranteedMessages();

    if (ProcessSystemMessages() != -1)
    {
        ProcessApplicationMessages();
    }
}

int MCSessionManager::ProcessSystemMessages()
{
    const size_t numMessages = SystemMessages.size();
    size_t i = 0;

    for (; i < numMessages && !SystemMessages.empty(); i++)
    {
        std::shared_ptr<MCFidpMessage> msg = SystemMessages.front();
        HandlePreSystemMessage(*msg);
        SystemMessages.pop_front();

        if (SystemCallback)
        {
            SystemCallback(*msg);
        }

        // Original behaviour: the "session lost" message is not returned to the free queue.
        if (SystemTypeOf(*msg) == DPSYS_SESSIONLOST)
        {
            return -1;
        }

        HandlePostSystemMessage(*msg);
        AddMessageToEmptyQueue(std::move(msg));
    }

    return static_cast<int>(i);
}

void MCSessionManager::ProcessGuaranteedMessages()
{
    for (size_t i = 0; i < Players.size(); i++)
    {
        const std::shared_ptr<MCFidpPlayer> player = Players[i];

        if (IsMyPlayer(*player))
        {
            continue;
        }

        while (const std::shared_ptr<MCFidpMessage> msg = player->NextMessageToProcess())
        {
            HandleApplicationMessage(msg);

            if (msg->Type() == PlayerRemovedType)
            {
                return;
            }
        }
    }
}

int MCSessionManager::ProcessApplicationMessages()
{
    const int numMessages = static_cast<int>(ApplicationMessages.size());

    while (!ApplicationMessages.empty())
    {
        const std::shared_ptr<MCFidpMessage> msg = ApplicationMessages.front();
        HandleApplicationMessage(msg);

        // Original behaviour: the "removed" message stays at the head of the queue (it is back in the free queue too).
        if (msg->Type() == PlayerRemovedType)
        {
            return -1;
        }

        ApplicationMessages.pop_front();
    }

    ProcessGuaranteedMessages();
    return numMessages;
}

int MCSessionManager::ReceiveThread()
{
    if (MyPlayer() == nullptr)
    {
        return -1;
    }

    for (MCFIVerifyMessage& verify : _VerifyMessages)
    {
        verify.Header = LinkupHeader(MCLinkupMessageType::Verify);
        verify.Count = 0;
    }

    int32_t result = 0;

    do
    {
        std::shared_ptr<MCFidpMessage> msg = GetMessageFromEmptyQueue();
        result = msg->ReceiveMessage(*DirectPlay);

        if (result != 0)
        {
            AddMessageToEmptyQueue(std::move(msg));
        }
        else if (msg->FromID == DPID_SYSMSG)
        {
            SystemMessages.push_back(std::move(msg));
        }
        else
        {
            RTProcessApplicationMessage(std::move(msg));
        }
    } while (result == 0);

    for (int32_t i = 0; i < MaxLinkupPlayers; i++)
    {
        MCFIVerifyMessage& verify = _VerifyMessages[i];

        if (verify.Count != 0)
        {
            SendPlainMessage(RTGetIDFromPlayerNumber(i), LinkupBytes(verify, verify.WireSize()));
        }
    }

    return 0;
}

void MCSessionManager::RTProcessApplicationMessage(std::shared_ptr<MCFidpMessage> msg)
{
    const std::shared_ptr<MCFidpPlayer> sender = GetPlayer(msg->FromID);

    if (sender == nullptr)
    {
        AddMessageToEmptyQueue(std::move(msg));
        return;
    }

    const uint16_t header = LinkupHeaderOf(msg->Bytes());
    const auto type = static_cast<MCLinkupMessageType>(header & FIMSG_TYPE_MASK);

    // The "removed" message bypasses the ordering (and stays at the queue's head until it is acted on), and the numbers
    // come before this machine can order anything: both are checked here, before they are queued.
    if ((type == MCLinkupMessageType::PlayerRemoved ||
         (type == MCLinkupMessageType::PlayerNumbers && !HasPlayerNumber)) &&
        !MaySend(type, msg->FromID))
    {
        SDL_Log("MCSessionManager: dropped linkup message %u from player %u", static_cast<uint32_t>(type), msg->FromID);
        AddMessageToEmptyQueue(std::move(msg));
        return;
    }

    if (HasPlayerNumber)
    {
        if (!IsPlainOrRemoved(header))
        {
            RTHandleNewGuaranteedMessage(std::move(msg), *sender);
        }
        else if (type == MCLinkupMessageType::Verify)
        {
            // Each entry is a tagger; this machine's slot holds the number of the message it acknowledges.
            const std::span<const uint8_t> bytes = msg->Bytes();
            const int count = bytes.size() > 2 ? bytes[2] : 0;
            const int32_t myNumber = MyPlayer()->PlayerNumber;

            for (int i = 0; i < count; i++)
            {
                // Port fix: only the entries that arrived are read (the original read on past the message).
                const size_t at = 3 + static_cast<size_t>(i) * 6 + myNumber;

                if (at >= bytes.size())
                {
                    break;
                }

                if (std::shared_ptr<MCFidpMessage> verified = sender->RemoveFromVerifyList(bytes[at]);
                    verified != nullptr)
                {
                    AddMessageToEmptyQueue(std::move(verified));
                }
            }

            AddMessageToEmptyQueue(std::move(msg));
        }
        else
        {
            ApplicationMessages.push_back(std::move(msg));
        }

        return;
    }

    if (type == MCLinkupMessageType::PlayerNumbers)
    {
        RTProcessPlayerNumbers(std::move(msg), *sender);
    }
    else if (IsPlainOrRemoved(header))
    {
        ApplicationMessages.push_back(std::move(msg));
    }
    else if (!LaunchedFromLobby)
    {
        PreIDReceivedMessages.push_back(std::move(msg));
    }
    else
    {
        AddMessageToEmptyQueue(std::move(msg));
    }
}

void MCSessionManager::RTProcessPlayerNumbers(std::shared_ptr<MCFidpMessage> msg, MCFidpPlayer& sender)
{
    // The server's player numbers: this machine (and everyone it knows) gets its number.
    const auto numbers = ReadLinkupMessage<MCFIPlayerNumbersMessage>(msg->Bytes());
    const size_t numPlayers = Players.size();

    if (!LaunchedFromLobby)
    {
        for (const auto& player : Players)
        {
            const auto found = std::ranges::find(numbers.PlayerIDs, player->Id);
            player->PlayerNumber =
                found != numbers.PlayerIDs.end() ? static_cast<int32_t>(found - numbers.PlayerIDs.begin()) : -1;
            player->HasPlayerNumber = true;
        }
    }
    else
    {
        ApplyLobbyPlayerNumbers(numbers);
    }

    ServerID = numbers.ServerNumber < MaxLinkupPlayers ? numbers.PlayerIDs[numbers.ServerNumber] : 0;
    const int32_t myNumber = MyPlayer()->PlayerNumber;

    if (myNumber == -1)
    {
        AddMessageToEmptyQueue(std::move(msg));
        return;
    }

    HasPlayerNumber = true;

    if (!LaunchedFromLobby)
    {
        size_t numbered = 0;

        for (const auto& player : Players)
        {
            if (player->PlayerNumber == -1)
            {
                GivePlayerAnID(*player);

                if (player->PlayerNumber < 0 || player->PlayerNumber >= MaxLinkupPlayers)
                {
                    return;
                }
            }

            numbered++;
        }

        if (numbered < numPlayers)
        {
            return;
        }
    }

    // Original behaviour: the numbers message is stored as the server's next incoming message and returned to the
    // free queue as well.
    const uint8_t sendCount = numbers.Tagger.SendCount[myNumber];
    sender.HandleIncomingMessage(msg, sendCount);
    sender.NextMessageToProcess();

    // Port fix: a server without a number gets no verify (the original wrote before the verify buffers).
    if (sender.PlayerNumber >= 0 && sender.PlayerNumber < MaxLinkupPlayers)
    {
        AddVerifyEntry(sender.PlayerNumber, sendCount);
    }

    AddMessageToEmptyQueue(std::move(msg));

    // The guaranteed messages that came before the numbers can be put in order now.
    const size_t numEarly = PreIDReceivedMessages.size();

    for (size_t i = 0; i < numEarly && !PreIDReceivedMessages.empty(); i++)
    {
        std::shared_ptr<MCFidpMessage> early = PreIDReceivedMessages.front();

        if (const std::shared_ptr<MCFidpPlayer> earlySender = GetPlayer(early->FromID); earlySender == nullptr)
        {
            AddMessageToEmptyQueue(std::move(early));
        }
        else
        {
            RTHandleNewGuaranteedMessage(std::move(early), *earlySender);
        }

        PreIDReceivedMessages.pop_front();
    }

    SendPreIDGuaranteedMessages();
}

void MCSessionManager::RTHandleNewGuaranteedMessage(std::shared_ptr<MCFidpMessage> msg, MCFidpPlayer& player)
{
    const uint8_t sendCount = LinkupSendCount(msg->Buffer(), MyPlayer()->PlayerNumber);

    if (player.PlayerNumber >= MaxLinkupPlayers || player.PlayerNumber < 0)
    {
        return;
    }

    bool stored = false;

    if (_VerifyMessages[player.PlayerNumber].Count < 0x28)
    {
        AddVerifyEntry(player.PlayerNumber, sendCount);
        stored = player.HandleIncomingMessage(msg, sendCount);
    }

    if (!stored)
    {
        AddMessageToEmptyQueue(std::move(msg));
    }
}

uint32_t MCSessionManager::RTGetIDFromPlayerNumber(int playerNumber)
{
    const std::shared_ptr<MCFidpPlayer> player = GetPlayerNumber(playerNumber);
    return player != nullptr ? player->Id : 0;
}

void MCSessionManager::UpdatePlayerGuaranteedMessages(MCFidpPlayer& player, uint64_t now)
{
    for (const std::shared_ptr<MCFidpMessage>& msg : player.VerifyList)
    {
        if ((player.HasPlayerNumber || msg->Type() == PlayerRemovedType) &&
            static_cast<uint64_t>(player.ResendDelay) * static_cast<uint64_t>(msg->TimesSent) <
                (now - msg->SendTime) / TicksPerMs())
        {
            SendPlainMessage(player.Id, msg->Bytes());
            msg->WasResent = true;
            msg->TimesSent++;
            msg->SendTime = now;
        }
    }
}

void MCSessionManager::UpdateGuaranteedMessages()
{
    const auto now = static_cast<uint64_t>(MCPort::PerformanceCounter());

    for (const auto& player : Players)
    {
        if (!IsMyPlayer(*player))
        {
            UpdatePlayerGuaranteedMessages(*player, now);
        }
    }
}
