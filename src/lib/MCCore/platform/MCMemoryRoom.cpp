#include "stdafx.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCNeverDestroyed.h"

namespace
{
    /// <summary>The system asked (none: nothing reported), and the generation of AskAgain readers compare.</summary>
    struct MCRoomState
    {
        std::mutex Lock;
        std::shared_ptr<MCMemoryRoom::MCSystem> System;
        std::atomic<uint64_t> Asked = 0;
    };

    MCRoomState& TheState()
    {
        static MCNeverDestroyed<MCRoomState> state;
        return *state;
    }

    std::shared_ptr<MCMemoryRoom::MCSystem> TheSystem()
    {
        MCRoomState& state = TheState();
        std::scoped_lock lock(state.Lock);
        return state.System;
    }
}

void MCMemoryRoom::SetSystem(std::shared_ptr<MCSystem> system)
{
    MCRoomState& state = TheState();

    {
        std::scoped_lock lock(state.Lock);
        state.System = std::move(system);
    }

    AskAgain();
}

void MCMemoryRoom::AskAgain()
{
    ++TheState().Asked;
}

uint64_t MCMemoryRoom::CardRoom(const MCCard& card, uint64_t held)
{
    const uint64_t given = card.Budget + held;
    return std::min(given > card.Used ? given - card.Used : 0, MostHdBytes);
}

MCMemoryRoom::MCReader::MCReader(std::string deviceName, MCClock now)
    : _DeviceName(std::move(deviceName)), _Now(std::move(now))
{
}

MCMemoryRoom::MCFrameRoom MCMemoryRoom::MCReader::Frame(uint64_t held)
{
    const std::shared_ptr<MCSystem> system = TheSystem();
    const bool systemShort = system != nullptr && system->Short();
    const uint64_t asked = TheState().Asked.load();
    const std::chrono::steady_clock::time_point now = _Now();

    if (!_NextRead.has_value() || now >= *_NextRead || asked != _Asked)
    {
        _Asked = asked;
        _NextRead = now + CardReadInterval;
        _Card = system != nullptr ? system->Card(_DeviceName) : std::nullopt;
        _Room = _Card.has_value() ? CardRoom(*_Card, held) : MostHdBytes;
        _CardShort = _Card.has_value() && _Card->Used > _Card->Budget;
    }

    // A card the system can't speak for is taken as sharing the machine's memory, so it follows the memory's signals.
    const bool shares = !_Card.has_value() || _Card->SharesMemory;
    return MCFrameRoom{.MemoryShort = systemShort || (shares && _CardShort),
                       .PictureBytes = systemShort && shares ? 0 : _Room};
}
