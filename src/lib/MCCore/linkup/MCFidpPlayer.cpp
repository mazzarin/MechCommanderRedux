#include "stdafx.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCSessionManager.h"
#include "lib/MCFatal.h"

MCFidpPlayer::MCFidpPlayer(uint32_t id, const DPNAME& name, uint32_t flags)
    : Name(LinkupName(name.ShortName, 0x7f)), LongName(LinkupName(name.LongName, 0xff)), Id(id), Flags(flags)
{
}

void MCFidpPlayer::AddToVerifyList(std::shared_ptr<MCFidpMessage> msg)
{
    std::scoped_lock lock(CriticalSection);
    msg->SendTime = static_cast<uint64_t>(MCPort::PerformanceCounter());

    if (!msg->WasResent)
    {
        msg->FirstSendTime = msg->SendTime;
    }

    msg->TimesSent = 1;
    VerifyList.push_back(std::move(msg));
}

int32_t MCFidpPlayer::AverageLatency()
{
    std::scoped_lock lock(CriticalSection);
    int32_t total = 0;
    int measured = 0;

    for (const int32_t latency : Latencies)
    {
        total += latency;

        if (latency > 0)
        {
            measured++;
        }
    }

    LastAverageLatency = measured < 1 ? 500 : total / measured;
    return LastAverageLatency;
}

uint8_t MCFidpPlayer::SendCountOf(const MCFidpMessage& msg) const
{
    return LinkupSendCount(msg.Buffer(), PlayerNumber);
}

std::shared_ptr<MCFidpMessage> MCFidpPlayer::RemoveFromVerifyList(uint8_t sendCount)
{
    std::scoped_lock lock(CriticalSection);
    const auto found =
        std::ranges::find_if(VerifyList, [&](const auto& msg) { return SendCountOf(*msg) == sendCount; });

    if (found == VerifyList.end())
    {
        return nullptr;
    }

    std::shared_ptr<MCFidpMessage> msg = *found;
    const auto now = static_cast<uint64_t>(MCPort::PerformanceCounter());

    if (!msg->WasResent)
    {
        LastLatency = static_cast<uint32_t>((now - msg->SendTime) / MCSessionManager::TicksPerMs());
        Latencies[LatencyIndex] = static_cast<int32_t>(LastLatency);
        LatencyIndex++;

        if (LatencyIndex >= LatencyHistory)
        {
            LatencyIndex = 0;
        }
    }

    VerifyList.erase(found);
    return msg;
}

bool MCFidpPlayer::IsVerifyListFull()
{
    return VerifyCountDifference() > 0x7f;
}

int MCFidpPlayer::VerifyCountDifference()
{
    std::scoped_lock lock(CriticalSection);

    if (VerifyList.empty())
    {
        return 0;
    }

    const uint8_t oldestCount = SendCountOf(*VerifyList.front());
    const uint8_t nextCount = static_cast<uint8_t>(OutgoingSendCount + 1);

    if (nextCount < oldestCount)
    {
        return nextCount + (0x100 - oldestCount);
    }

    return nextCount - oldestCount;
}

bool MCFidpPlayer::HandleIncomingMessage(std::shared_ptr<MCFidpMessage> msg, int sendCount)
{
    Assert((ReadLinkupMessage<MCFIMessageHeader>(msg->Bytes()).Header & FIMSG_GUARANTEED) != 0, 0,
           "Should not call HandleIncomingMessage for non-guaranteed");
    const int window = sendCount < NextIncomingSendCount ? sendCount + 0x100 - NextIncomingSendCount
                                                         : sendCount - NextIncomingSendCount;

    if (window < 0 || window > 0x7f || IncomingMessages[sendCount] != nullptr)
    {
        return false;
    }

    IncomingMessages[sendCount] = std::move(msg);
    NumIncomingMessages++;

    if (sendCount == NextIncomingSendCount)
    {
        SetNextIncomingSendCount();
    }

    return true;
}

void MCFidpPlayer::SetNextIncomingSendCount()
{
    NextIncomingSendCount++;
    const uint8_t start = NextIncomingSendCount;

    while (IncomingMessages[NextIncomingSendCount] != nullptr)
    {
        NextIncomingSendCount++;
        Assert(NextIncomingSendCount != start, 0, "Guaranteed message send_count problem2");
    }
}

std::shared_ptr<MCFidpMessage> MCFidpPlayer::NextMessageToProcess()
{
    std::shared_ptr<MCFidpMessage> msg = std::exchange(IncomingMessages[NextIncomingToProcess], nullptr);

    if (msg == nullptr)
    {
        return nullptr;
    }

    NextIncomingToProcess++;
    NumIncomingMessages--;
    return msg;
}

void MCFidpPlayer::JoinGroup(uint32_t groupID)
{
    Groups.push_back(groupID);
}

void MCFidpPlayer::LeaveGroup(uint32_t groupID)
{
    if (const auto found = std::ranges::find(Groups, groupID); found != Groups.end())
    {
        Groups.erase(found);
    }
}

bool MCFidpPlayer::IsInGroup(uint32_t groupID) const
{
    return std::ranges::contains(Groups, groupID);
}
