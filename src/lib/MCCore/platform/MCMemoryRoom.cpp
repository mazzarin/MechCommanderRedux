#include "stdafx.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCNeverDestroyed.h"

namespace
{
    /// <summary>
    /// The system asked (none: nothing reported), and the generation of AskAgain readers compare; the machine and card
    /// planned as (-hdmemory, -hdcard; none: the machine's own); the card a reader last read, for the ledger's line; the
    /// readers made, and the newest living one's number (0: none) and device, for DrawnCard.
    /// </summary>
    struct MCRoomState
    {
        std::mutex Lock;
        std::shared_ptr<MCMemoryRoom::MCSystem> System;
        std::atomic<uint64_t> Asked = 0;
        std::optional<uint64_t> MachineBytes;
        std::optional<uint64_t> CardBytes;
        std::optional<MCMemoryRoom::MCCard> LastCard;
        uint64_t Readers = 0;
        uint64_t DrawnReader = 0;
        std::string DrawnDevice;
    };

    MCRoomState& TheState()
    {
        static MCNeverDestroyed<MCRoomState> state;
        return *state;
    }

    /// <summary>The machine's memory as SDL reports it (in MiB), in bytes; 0 when it can't say.</summary>
    uint64_t SystemMemoryBytes()
    {
        constexpr int BytesPerMibShift = 20;
        const int mib = SDL_GetSystemRAM();
        return mib > 0 ? static_cast<uint64_t>(mib) << BytesPerMibShift : 0;
    }

    std::shared_ptr<MCMemoryRoom::MCSystem> TheSystem()
    {
        MCRoomState& state = TheState();
        std::scoped_lock lock(state.Lock);
        return state.System;
    }

    /// <summary>
    /// <paramref name="card"/>, unless it says it gives the game nothing (DXGI's local budget can read 0): a card the
    /// system can't speak for.
    /// </summary>
    std::optional<MCMemoryRoom::MCCard> Spoken(std::optional<MCMemoryRoom::MCCard> card)
    {
        return card.has_value() && card->Budget > 0 ? card : std::nullopt;
    }
}

uint64_t MCMemoryRoom::MCSystem::TotalBytes()
{
    return SystemMemoryBytes();
}

uint64_t MCMemoryRoom::TotalBytes()
{
    MCRoomState& state = TheState();

    if (std::scoped_lock lock(state.Lock); state.MachineBytes.has_value())
    {
        return *state.MachineBytes;
    }

    const std::shared_ptr<MCSystem> system = TheSystem();
    return system != nullptr ? system->TotalBytes() : SystemMemoryBytes();
}

void MCMemoryRoom::SetMachineBytes(std::optional<uint64_t> bytes)
{
    MCRoomState& state = TheState();

    {
        std::scoped_lock lock(state.Lock);
        state.MachineBytes = bytes;
    }

    AskAgain();
}

void MCMemoryRoom::SetCardBytes(std::optional<uint64_t> bytes)
{
    MCRoomState& state = TheState();

    {
        std::scoped_lock lock(state.Lock);
        state.CardBytes = bytes;
        state.LastCard.reset();
    }

    AskAgain();
}

std::string MCMemoryRoom::LedgerLine()
{
    MCRoomState& state = TheState();
    std::optional<MCCard> card;

    {
        std::scoped_lock lock(state.Lock);
        card = state.LastCard;
    }

    const bool shared = !card.has_value() || card->SharesMemory;
    const MCMemoryLedger::MCRoom room{.MachineBytes = TotalBytes(),
                                      .Shared = shared,
                                      .CardBytes = shared ? std::nullopt : std::optional<uint64_t>(card->Budget)};
    const std::shared_ptr<MCSystem> system = TheSystem();
    const std::optional<MCMemoryLedger::MCProcessFigures> process =
        system != nullptr ? system->Process() : std::nullopt;
    return MCMemoryLedger::Line(room, process.value_or(MCMemoryLedger::MCProcessFigures{}));
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

bool MCMemoryRoom::SharesMemory()
{
    MCRoomState& state = TheState();
    std::scoped_lock lock(state.Lock);
    return !state.LastCard.has_value() || state.LastCard->SharesMemory;
}

MCMemoryRoom::MCArtRoom MCMemoryRoom::ArtRoom()
{
    using MCMemoryLedger::MCHolder;
    // The HD art's own holders, which the room is for.
    constexpr std::array ArtHolders{MCHolder::HdFrames,     MCHolder::HdColors, MCHolder::HdFramesLetGo,
                                    MCHolder::HdReadAhead,  MCHolder::HdHold,   MCHolder::HdKeptStreams,
                                    MCHolder::GpuHdPictures};
    const bool shared = SharesMemory();
    uint64_t fixed = GameItselfBytes;

    for (size_t holder = 0; holder < MCMemoryLedger::HolderCount; ++holder)
    {
        const auto named = static_cast<MCHolder>(holder);

        if (std::ranges::contains(ArtHolders, named) ||
            (!shared && MCMemoryLedger::SideOf(named) == MCMemoryLedger::MCSide::Gpu))
        {
            continue;
        }

        const MCMemoryLedger::MCFigures figures = MCMemoryLedger::Of(named);
        fixed += static_cast<uint64_t>(std::max<int64_t>(figures.Allocated + figures.Retired, 0));
    }

    const uint64_t ceiling =
        MCMemoryLedger::Ceiling(MCMemoryLedger::MCRoom{.MachineBytes = TotalBytes(), .Shared = shared});
    return MCArtRoom{.Bytes = ceiling > fixed ? ceiling - fixed : 0, .Shared = shared};
}

uint64_t MCMemoryRoom::CardRoom(const MCCard& card, uint64_t held)
{
    // Port-only (SPEC-memory.md's rule): a card of its own keeps within 90% of what its driver gives the game; on shared
    // memory the plan's share keeps the pictures (MCHdArt::NextFrame), under Metal's working set.
    const uint64_t given = (card.SharesMemory ? card.Budget : MCMemoryLedger::CardCeiling(card.Budget)) + held;
    return given > card.Used ? given - card.Used : 0;
}

std::optional<MCMemoryRoom::MCCard> MCMemoryRoom::DrawnCard()
{
    MCRoomState& state = TheState();
    std::shared_ptr<MCSystem> system;
    std::string device;

    {
        std::scoped_lock lock(state.Lock);

        if (state.DrawnReader == 0)
        {
            return std::nullopt;
        }

        system = state.System;
        device = state.DrawnDevice;
    }

    return system != nullptr ? Spoken(system->Card(device)) : std::nullopt;
}

MCMemoryRoom::MCReader::MCReader(std::string deviceName, MCClock now)
    : _DeviceName(std::move(deviceName)), _Now(std::move(now))
{
    MCRoomState& state = TheState();
    std::scoped_lock lock(state.Lock);
    _Number = ++state.Readers;
    state.DrawnReader = _Number;
    state.DrawnDevice = _DeviceName;
}

MCMemoryRoom::MCReader::~MCReader()
{
    MCRoomState& state = TheState();
    std::scoped_lock lock(state.Lock);

    if (state.DrawnReader == _Number)
    {
        state.DrawnReader = 0;
        state.DrawnDevice.clear();
    }
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
        // A card that says it gives the game nothing is one the system can't speak for: taken as over its budget, every
        // frame would be short and the hold given back.
        _Card = system != nullptr ? Spoken(system->Card(_DeviceName)) : std::nullopt;

        MCRoomState& state = TheState();
        std::scoped_lock lock(state.Lock);

        // -hdcard: a card of the game's own, holding what the system says the game holds on the GPU.
        if (state.CardBytes.has_value())
        {
            _Card = MCCard{.Budget = *state.CardBytes, .Used = _Card.has_value() ? _Card->Used : held};
        }

        state.LastCard = _Card;
        _Room = _Card.has_value() ? CardRoom(*_Card, held) : MostHdBytes;
        _CardShort = _Card.has_value() && _Card->Used > _Card->Budget;
    }

    // A card the system can't speak for is taken as sharing the machine's memory, so it follows the memory's signals.
    const bool shares = !_Card.has_value() || _Card->SharesMemory;
    return MCFrameRoom{.MemoryShort = systemShort || (shares && _CardShort),
                       .PictureBytes = systemShort && shares ? 0 : _Room};
}
