#include "stdafx.h"
#include "platform/MCUploadSlots.h"

#include "platform/MCFrameLog.h"
#include "platform/MCGpuCounted.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <limits>

namespace
{
    /// <summary>The slots' bytes set for timing runs (-hdslots); 0: by the machine's memory.</summary>
    std::atomic<size_t> SetSlotBytes{0};

    /// <summary>The frame log's counts, by kind (MCFrameLog::Count keeps the names it's given).</summary>
    constexpr std::array<std::string_view, MCUploadSlots::KindCount> WaitCounts = {
        "upload slot waits (frame)", "upload slot waits (near)", "upload slot waits (fill)",
        "upload slot waits (ground)"};
    constexpr std::array<std::string_view, MCUploadSlots::KindCount> WaitedCounts = {
        "upload slot wait us (frame)", "upload slot wait us (near)", "upload slot wait us (fill)",
        "upload slot wait us (ground)"};
    constexpr std::array<std::string_view, MCUploadSlots::KindCount> RefusedCounts = {
        "upload slots full (frame)", "upload slots full (near)", "upload slots full (fill)",
        "upload slots full (ground)"};

    /// <summary>SDL's transfer buffer, mapped once, and the GPU's fences.</summary>
    class MCSdlBacking final : public MCUploadSlots::MCBacking
    {
    public:
        MCSdlBacking(std::shared_ptr<SDL_GPUDevice> device, MCCountedGpuHandle<SDL_GPUTransferBuffer> buffer,
                     std::span<uint8_t> memory)
            : _Device(std::move(device)), _Buffer(std::move(buffer)), _Memory(memory)
        {
        }

        ~MCSdlBacking() override { SDL_UnmapGPUTransferBuffer(_Device.get(), _Buffer.get()); }

        MCSdlBacking(const MCSdlBacking&) = delete;
        MCSdlBacking& operator=(const MCSdlBacking&) = delete;

        std::span<uint8_t> Memory() override { return _Memory; }
        SDL_GPUTransferBuffer* Buffer() override { return _Buffer.get(); }
        bool Done(SDL_GPUFence* fence) override { return SDL_QueryGPUFence(_Device.get(), fence); }
        bool Wait(SDL_GPUFence* fence) override { return SDL_WaitForGPUFences(_Device.get(), true, &fence, 1); }
        void Release(SDL_GPUFence* fence) override { SDL_ReleaseGPUFence(_Device.get(), fence); }

        // On managed storage (an Intel Mac's card) this marks the buffer modified; elsewhere it does nothing.
        void Flush() override { SDL_UnmapGPUTransferBuffer(_Device.get(), _Buffer.get()); }

    private:
        std::shared_ptr<SDL_GPUDevice> _Device;
        MCCountedGpuHandle<SDL_GPUTransferBuffer> _Buffer;
        std::span<uint8_t> _Memory;
    };

    constexpr size_t AlignUp(size_t offset)
    {
        return (offset + MCUploadSlots::Alignment - 1) & ~(MCUploadSlots::Alignment - 1);
    }
}

std::string_view MCUploadSlots::KindName(MCKind kind)
{
    switch (kind)
    {
        case MCKind::Frame:
            return "frame";
        case MCKind::Near:
            return "near";
        case MCKind::Fill:
            return "fill";
        case MCKind::Ground:
            return "ground";
    }

    return "?";
}

MCUploadSlots::MCLease::MCLease(MCLease&& other) noexcept
    : _Pool(std::move(other._Pool)), _First(other._First), _Count(other._Count), _Used(other._Used), _Kind(other._Kind)
{
    other._Count = 0;
    other._Used = 0;
}

auto MCUploadSlots::MCLease::operator=(MCLease&& other) noexcept -> MCLease&
{
    if (this != &other)
    {
        Drop();
        _Pool = std::move(other._Pool);
        _First = other._First;
        _Count = other._Count;
        _Used = other._Used;
        _Kind = other._Kind;
        other._Count = 0;
        other._Used = 0;
    }

    return *this;
}

MCUploadSlots::MCLease::~MCLease()
{
    Drop();
}

void MCUploadSlots::MCLease::Drop()
{
    if (_Pool != nullptr)
    {
        _Pool->GiveBack(*this);
        _Pool.reset();
    }
}

size_t MCUploadSlots::MCLease::Offset() const
{
    return static_cast<size_t>(_First) * SlotBytes;
}

size_t MCUploadSlots::MCLease::Size() const
{
    return static_cast<size_t>(_Count) * SlotBytes;
}

SDL_GPUTransferBuffer* MCUploadSlots::MCLease::Buffer() const
{
    return _Pool != nullptr ? _Pool->_Buffer : nullptr;
}

std::optional<size_t> MCUploadSlots::MCLease::Place(size_t bytes)
{
    const size_t at = AlignUp(_Used);

    if (_Pool == nullptr || at > Size() || bytes > Size() - at)
    {
        return std::nullopt;
    }

    _Used = at + bytes;
    return Offset() + at;
}

std::span<uint8_t> MCUploadSlots::MCLease::At(size_t offset, size_t bytes) const
{
    if (_Pool == nullptr || offset < Offset() || offset + bytes > Offset() + Size())
    {
        return {};
    }

    return _Pool->_Memory.subspan(offset, bytes);
}

std::expected<std::shared_ptr<MCUploadSlots>, std::string> MCUploadSlots::Make(
    const std::shared_ptr<SDL_GPUDevice>& device, size_t bytes)
{
    const size_t slots = std::max<size_t>(bytes / SlotBytes, NearSlots + 1);
    const size_t size = slots * SlotBytes;

    if (size > std::numeric_limits<uint32_t>::max())
    {
        return std::unexpected(std::format("the upload slots: {} bytes are more than a transfer buffer holds", size));
    }

    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = static_cast<uint32_t>(size);
    MCCountedGpuHandle<SDL_GPUTransferBuffer> buffer =
        MCGpuCounted::MakeTransferBuffer(device, info, MCMemoryLedger::MCHolder::GpuTransfers);

    if (buffer == nullptr)
    {
        return std::unexpected(
            std::format("SDL_CreateGPUTransferBuffer(the upload slots, {} bytes): {}", size, SDL_GetError()));
    }

    // Mapped for good: never cycled, so it names the buffer's own memory, which a dedicated allocation keeps in place.
    void* const memory = SDL_MapGPUTransferBuffer(device.get(), buffer.get(), false);

    if (memory == nullptr)
    {
        return std::unexpected(std::format("SDL_MapGPUTransferBuffer(the upload slots): {}", SDL_GetError()));
    }

    SDL_Log("MCUploadSlots: %zu MB in %zu slots of %zu MB", size >> 20, slots, SlotBytes >> 20);
    return MakeWith(
        std::make_unique<MCSdlBacking>(device, std::move(buffer), std::span(static_cast<uint8_t*>(memory), size)));
}

std::shared_ptr<MCUploadSlots> MCUploadSlots::MakeWith(std::unique_ptr<MCBacking> backing)
{
    return std::shared_ptr<MCUploadSlots>(new MCUploadSlots(std::move(backing)));
}

size_t MCUploadSlots::BytesFor(uint64_t machineBytes)
{
    if (const size_t set = SetSlotBytes.load(); set != 0)
    {
        return set;
    }

    return machineBytes >= LargeFromMachineBytes ? LargeBytes : LeastBytes;
}

void MCUploadSlots::SetBytes(std::optional<size_t> bytes)
{
    SetSlotBytes.store(bytes.has_value() ? std::clamp(*bytes, (NearSlots + 1) * SlotBytes, MostBytes) : 0);
}

MCUploadSlots::MCUploadSlots(std::unique_ptr<MCBacking> backing) : _Backing(std::move(backing))
{
    _Buffer = _Backing->Buffer();
    _Memory = _Backing->Memory();
    _States.assign(_Memory.size() / SlotBytes, MCSlot{});
    _FreeSlots = _States.size();
}

MCUploadSlots::~MCUploadSlots()
{
    WaitForAll();
}

uint32_t MCUploadSlots::SlotsFor(size_t bytes)
{
    const size_t slots = std::max<size_t>((bytes + SlotBytes - 1) / SlotBytes, 1);
    return static_cast<uint32_t>(std::min<size_t>(slots, std::numeric_limits<uint32_t>::max()));
}

size_t MCUploadSlots::MostFor(MCKind kind) const
{
    // A kind kept off the near slots still takes one at least: slots made fewer than NearSlots (a test's) aren't shut.
    constexpr size_t LeastSlots = 1;
    const size_t past = _States.size() > NearSlots ? _States.size() - NearSlots : 0;

    switch (kind)
    {
        case MCKind::Fill:
            return std::max(past * FillShareNumerator / FillShareDenominator, LeastSlots);
        case MCKind::Ground:
            return std::max(past, LeastSlots);
        case MCKind::Frame:
        case MCKind::Near:
            break;
    }

    return _States.size();
}

void MCUploadSlots::Recount()
{
    _Tally.InUse = _States.size() - _FreeSlots;
    _Tally.PeakInUse = std::max(_Tally.PeakInUse, _Tally.InUse);
    _Tally.InFlight = _Sent.size();
}

std::optional<MCUploadSlots::MCLease> MCUploadSlots::TakeLocked(MCKind kind, size_t bytes)
{
    const uint32_t count = SlotsFor(bytes);
    MCKindTally& tally = _Tally.Kinds[static_cast<size_t>(kind)];

    // The fill and the ground leave the last NearSlots free slots free, and the fill holds no more than its share.
    const size_t kept = kind == MCKind::Fill || kind == MCKind::Ground ? NearSlots : 0;

    if (count > _FreeSlots || _FreeSlots - count < kept || tally.Held + count > MostFor(kind))
    {
        return std::nullopt;
    }

    // The first run of free neighbours long enough.
    uint32_t run = 0;

    for (uint32_t i = 0; i < _States.size(); ++i)
    {
        run = Free(i) ? run + 1 : 0;

        if (run == count)
        {
            const uint32_t first = i + 1 - count;

            for (uint32_t at = first; at <= i; ++at)
            {
                _States[at].Leased = true;
            }

            _FreeSlots -= count;
            tally.Held += count;
            ++tally.Takes;
            Recount();
            MCLease lease;
            lease._Pool = shared_from_this();
            lease._First = first;
            lease._Count = count;
            lease._Kind = kind;
            return lease;
        }
    }

    return std::nullopt;
}

std::optional<MCUploadSlots::MCLease> MCUploadSlots::TryTake(MCKind kind, size_t bytes)
{
    std::scoped_lock lock(_Lock);

    if (SlotsFor(bytes) <= MostFor(kind))
    {
        if (std::optional<MCLease> lease = TakeLocked(kind, bytes); lease.has_value())
        {
            return lease;
        }

        ReclaimLocked();

        if (std::optional<MCLease> lease = TakeLocked(kind, bytes); lease.has_value())
        {
            return lease;
        }
    }

    ++_Tally.Kinds[static_cast<size_t>(kind)].Refused;
    MCFrameLog::Count(RefusedCounts[static_cast<size_t>(kind)]);
    return std::nullopt;
}

std::optional<MCUploadSlots::MCLease> MCUploadSlots::Take(MCKind kind, size_t bytes, std::stop_token stop, MCWait wait)
{
    std::unique_lock lock(_Lock);
    MCKindTally& tally = _Tally.Kinds[static_cast<size_t>(kind)];

    if (SlotsFor(bytes) > MostFor(kind))
    {
        ++tally.Refused;
        MCFrameLog::Count(RefusedCounts[static_cast<size_t>(kind)]);
        return std::nullopt;
    }

    std::optional<MCLease> lease = TakeLocked(kind, bytes);

    if (lease.has_value())
    {
        return lease;
    }

    ReclaimLocked();
    lease = TakeLocked(kind, bytes);

    if (lease.has_value())
    {
        return lease;
    }

    // Backpressure: the copies sent before free the slots as the GPU runs them; nothing says when, so the fences are
    // looked at again every WaitPoll (FillWaitPoll for the fill), or as soon as a lease is given back or the game's
    // thread finds copies done (Reclaim). Waiting on copies sent only, once none are left nothing will free a slot but
    // the waiter itself.
    const auto start = std::chrono::steady_clock::now();
    const std::chrono::microseconds poll = kind == MCKind::Fill ? FillWaitPoll : WaitPoll;

    while (!lease.has_value() && !stop.stop_requested() && (wait == MCWait::Free || !_Sent.empty()))
    {
        _Freed.wait_for(lock, poll);
        ReclaimLocked();
        lease = TakeLocked(kind, bytes);
    }

    const auto waited = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
    ++tally.Waits;
    tally.Waited += waited;
    MCFrameLog::Count(WaitCounts[static_cast<size_t>(kind)]);
    MCFrameLog::Count(WaitedCounts[static_cast<size_t>(kind)],
                      std::chrono::duration_cast<std::chrono::microseconds>(waited).count());

    if (!lease.has_value())
    {
        ++tally.Refused;
        MCFrameLog::Count(RefusedCounts[static_cast<size_t>(kind)]);
    }

    return lease;
}

void MCUploadSlots::GiveBack(MCLease& lease)
{
    if (lease._Count == 0)
    {
        return;
    }

    size_t freed = 0;

    {
        std::scoped_lock lock(_Lock);

        for (uint32_t at = lease._First; at < lease._First + lease._Count; ++at)
        {
            _States[at].Leased = false;
            freed += Free(at) ? 1 : 0;
        }

        _FreeSlots += freed;
        _Tally.Kinds[static_cast<size_t>(lease._Kind)].Held -= lease._Count;
        Recount();
    }

    lease._Count = 0;
    lease._Used = 0;

    if (freed != 0)
    {
        _Freed.notify_all();
    }
}

void MCUploadSlots::Send(std::span<MCLease> leases, std::span<const std::shared_ptr<const MCLease>> shared,
                         SDL_GPUFence* fence, size_t bytes)
{
    if (fence == nullptr)
    {
        // Nothing of theirs will run: the leases free now; the shared ones as they go.
        for (MCLease& lease : leases)
        {
            lease.Drop();
        }

        return;
    }

    MCBatch batch;
    batch.Fence = fence;
    batch.SentAt = std::chrono::steady_clock::now();
    batch.Bytes = bytes;

    for (const MCLease& lease : leases)
    {
        if (lease._Pool.get() == this && lease._Count != 0)
        {
            batch.Runs.emplace_back(lease._First, lease._Count);
        }
    }

    for (const std::shared_ptr<const MCLease>& lease : shared)
    {
        if (lease != nullptr && lease->_Pool.get() == this && lease->_Count != 0)
        {
            batch.Runs.emplace_back(lease->_First, lease->_Count);
        }
    }

    size_t freed = 0;

    {
        std::scoped_lock lock(_Lock);

        for (const auto& [first, count] : batch.Runs)
        {
            for (uint32_t at = first; at < first + count; ++at)
            {
                ++_States[at].Sends;
            }
        }

        // The leases are the batch's now: no lease holds their slots, which free once its fence has signalled.
        for (MCLease& lease : leases)
        {
            if (lease._Pool.get() == this && lease._Count != 0)
            {
                for (uint32_t at = lease._First; at < lease._First + lease._Count; ++at)
                {
                    _States[at].Leased = false;
                    freed += Free(at) ? 1 : 0;
                }

                _Tally.Kinds[static_cast<size_t>(lease._Kind)].Held -= lease._Count;
            }
        }

        _FreeSlots += freed;
        _Sent.push_back(std::move(batch));
        ++_Tally.Sent;
        Recount();
    }

    for (MCLease& lease : leases)
    {
        if (lease._Pool.get() == this)
        {
            lease._Count = 0;
            lease._Used = 0;
            lease._Pool.reset();
        }
    }
}

size_t MCUploadSlots::ReclaimLocked()
{
    size_t freed = 0;
    const auto now = std::chrono::steady_clock::now();

    // Commands run in the order submitted on one queue, but leases are sent from several threads: each fence is looked at.
    std::erase_if(_Sent,
                  [&](const MCBatch& batch)
                  {
                      if (!_Backing->Done(batch.Fence))
                      {
                          return false;
                      }

                      for (const auto& [first, count] : batch.Runs)
                      {
                          for (uint32_t at = first; at < first + count; ++at)
                          {
                              --_States[at].Sends;
                              freed += Free(at) ? 1 : 0;
                          }
                      }

                      if (batch.Bytes >= TimedLeastBytes)
                      {
                          const auto took = std::chrono::duration_cast<std::chrono::nanoseconds>(now - batch.SentAt);
                          _Tally.TimedBytes += static_cast<int64_t>(batch.Bytes);
                          _Tally.TimedTime += took;

                          if (took.count() > 0)
                          {
                              _CopyBytesPerSecond =
                                  std::max(_CopyBytesPerSecond, static_cast<double>(batch.Bytes) /
                                                                    std::chrono::duration<double>(took).count());
                          }
                      }

                      _Backing->Release(batch.Fence);
                      return true;
                  });

    _FreeSlots += freed;
    Recount();
    return freed;
}

size_t MCUploadSlots::Reclaim()
{
    size_t freed = 0;

    {
        std::scoped_lock lock(_Lock);
        freed = ReclaimLocked();
    }

    if (freed != 0)
    {
        _Freed.notify_all();
    }

    return freed;
}

bool MCUploadSlots::WaitForAll()
{
    bool waited = true;

    {
        std::scoped_lock lock(_Lock);

        for (MCBatch& batch : _Sent)
        {
            // A fence that can't be waited for is kept (let go before its commands have run, it may be handed out again
            // and seem done too soon), and so are its slots.
            if (!_Backing->Wait(batch.Fence))
            {
                SDL_Log("MCUploadSlots: can't wait for an upload's copies (%s); its slots stay taken", SDL_GetError());
                waited = false;
            }
        }

        ReclaimLocked();
    }

    _Freed.notify_all();
    return waited;
}

void MCUploadSlots::Flush()
{
    _Backing->Flush();
}

double MCUploadSlots::CopyBytesPerSecond() const
{
    std::scoped_lock lock(_Lock);
    return _CopyBytesPerSecond;
}

MCUploadSlots::MCTally MCUploadSlots::Tally() const
{
    std::scoped_lock lock(_Lock);
    return _Tally;
}

namespace
{
    /// <summary>The frame log's count of uploads past the slots, by kind.</summary>
    constexpr std::array<std::string_view, MCUploadSlots::KindCount> PastSlotsCounts = {
        "uploads past the slots (frame)", "uploads past the slots (near)", "uploads past the slots (fill)",
        "uploads past the slots (ground)"};
}

MCUploadBatch::MCUploadBatch(std::shared_ptr<SDL_GPUDevice> device, std::shared_ptr<MCUploadSlots> slots,
                             MCUploadSlots::MCKind kind)
    : _Device(std::move(device)), _Slots(std::move(slots)), _Kind(kind)
{
}

MCUploadBatch::MCStaged MCUploadBatch::Stage(size_t bytes, size_t leaseBytes, bool wait)
{
    _Staged += bytes;

    if (_Slots != nullptr && !_Leases.empty())
    {
        if (const std::optional<size_t> at = _Leases.back().Place(bytes); at.has_value())
        {
            return {_Leases.back().At(*at, bytes), _Slots->Buffer(), static_cast<uint32_t>(*at)};
        }
    }

    if (_Slots != nullptr)
    {
        const size_t lease = std::max(bytes, leaseBytes);
        std::optional<MCUploadSlots::MCLease> taken =
            wait ? _Slots->Take(_Kind, lease, {}, MCUploadSlots::MCWait::Sent) : _Slots->TryTake(_Kind, lease);

        if (taken.has_value())
        {
            _Leases.push_back(std::move(*taken));
            const std::optional<size_t> at = _Leases.back().Place(bytes);
            return {_Leases.back().At(*at, bytes), _Slots->Buffer(), static_cast<uint32_t>(*at)};
        }
    }

    // Past the slots: memory of its own, as the frames before the slots sent a burst.
    MCFrameLog::Count(PastSlotsCounts[static_cast<size_t>(_Kind)]);
    return StageOwn(bytes, std::max(bytes, leaseBytes));
}

MCUploadBatch::MCStaged MCUploadBatch::StageOwn(size_t bytes, size_t size)
{
    if (!_Own.empty())
    {
        MCOwn& last = _Own.back();
        const size_t at = AlignUp(last.Used);

        if (at <= last.Size && bytes <= last.Size - at)
        {
            last.Used = at + bytes;
            return {std::span(last.Memory + at, bytes), last.Buffer.get(), static_cast<uint32_t>(at)};
        }
    }

    MCOwn own;

    if (size <= std::numeric_limits<uint32_t>::max() && _Device != nullptr)
    {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = static_cast<uint32_t>(size);
        own.Buffer = MCGpuCounted::MakeTransferBuffer(_Device, info, MCMemoryLedger::MCHolder::GpuTransfers);
        own.Memory = own.Buffer != nullptr
                         ? static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(_Device.get(), own.Buffer.get(), false))
                         : nullptr;
    }

    if (own.Memory == nullptr)
    {
        SDL_Log("MCUploadBatch: an upload of %zu bytes past the slots has nowhere to go (%s); it isn't sent", bytes,
                SDL_GetError());
        _Lost.resize(std::max(_Lost.size(), bytes));
        return {std::span(_Lost).first(bytes), nullptr, 0};
    }

    own.Size = size;
    own.Used = bytes;
    _Own.push_back(std::move(own));
    return {std::span(_Own.back().Memory, bytes), _Own.back().Buffer.get(), 0};
}

void MCUploadBatch::Carry(std::shared_ptr<const MCUploadSlots::MCLease> lease, size_t bytes)
{
    if (lease != nullptr)
    {
        // Each lease once in the batch however many of its uploads are in it (its slots counted sent once).
        if (std::ranges::find(_Carried, lease) == _Carried.end())
        {
            _Carried.push_back(std::move(lease));
        }

        _CarriedBytes += bytes;
    }
}

void MCUploadBatch::Clear()
{
    if (_Device != nullptr)
    {
        for (const MCOwn& own : _Own)
        {
            if (own.Memory != nullptr)
            {
                SDL_UnmapGPUTransferBuffer(_Device.get(), own.Buffer.get());
            }
        }
    }

    _Leases.clear();
    _Carried.clear();
    _Own.clear();
    _Staged = 0;
    _CarriedBytes = 0;
}

SDL_GPUCommandBuffer* MCUploadBatch::Begin()
{
    if (_Slots != nullptr)
    {
        _Slots->Flush();
    }

    for (MCOwn& own : _Own)
    {
        SDL_UnmapGPUTransferBuffer(_Device.get(), own.Buffer.get());
        own.Memory = nullptr;
    }

    return SDL_AcquireGPUCommandBuffer(_Device.get());
}

std::expected<void, std::string> MCUploadBatch::End(SDL_GPUCommandBuffer& commands)
{
    SDL_GPUFence* const fence = SDL_SubmitGPUCommandBufferAndAcquireFence(&commands);

    if (_Slots != nullptr)
    {
        _Slots->Send(_Leases, _Carried, fence, _Staged + _CarriedBytes);
    }

    // The buffers of its own were unmapped in Begin: SDL keeps them until their copies have run.
    _Leases.clear();
    _Carried.clear();
    _Own.clear();
    _Staged = 0;
    _CarriedBytes = 0;

    if (fence == nullptr)
    {
        return std::unexpected(std::string("SDL_SubmitGPUCommandBufferAndAcquireFence (the uploads): ") +
                               SDL_GetError());
    }

    if (_Slots == nullptr)
    {
        SDL_ReleaseGPUFence(_Device.get(), fence);
    }

    return {};
}
