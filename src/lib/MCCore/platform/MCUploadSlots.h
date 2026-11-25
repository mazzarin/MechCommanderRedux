#pragma once

#include "lib/MCOptionalRef.h"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// Port-only (SPEC-memory.md's "A frame's path", row 5: the upload slots): the memory every upload to the GPU goes up
/// through, made at the start and kept for the session, in slots of <see cref="SlotBytes"/>; while a load's fill runs,
/// more slots in a buffer of their own (<see cref="Grow"/>), let go once it's done and their copies have run. A writer takes a run of
/// slots (a lease), writes its uploads into it, records their copies on a command buffer and sends the lease with that
/// command buffer's fence; the slots are free again only once every fence sent with them has signalled and the lease is
/// gone, never sooner (SDL's cycling, which can make new backing buffers, isn't used). A reader waits for free slots
/// (<see cref="Take"/>), so the fill has backpressure; the game's thread never does (<see cref="TryTake"/>, which says
/// none instead), or only for copies already sent (<see cref="MCWait::Sent"/>: the ground's at a load). A few slots
/// (<see cref="NearSlots"/>) are kept from the fill and the ground for the frames' own and the read-ahead's near frames,
/// and the fill holds at most <see cref="FillShare"/> of the rest unsent.
/// </summary>
/// <remarks>
/// Threads: any. The slots are write-combined on Metal (SDL_gpu_metal.m's upload buffers): they're written in order,
/// never read back. A transfer buffer is a dedicated allocation on Vulkan, so its memory never moves (SDL's defrag
/// leaves it), and mapping it without cycling only names its memory: it's mapped once, at the start.
/// </remarks>
class MCUploadSlots : public std::enable_shared_from_this<MCUploadSlots>
{
    /// <summary>Only <see cref="MakeWith"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A slot's bytes: an upload bigger than one takes a run of neighbours.</summary>
    static constexpr size_t SlotBytes = size_t{2} << 20;
    /// <summary>
    /// The slots' bytes in all, kept for the session (you, 17 Nov: "the floor needs to be 128"): play needs no more (20
    /// Nov, the A10's 4 GB card: no waits for a slot in play at 128, 256 or 512 MB).
    /// </summary>
    static constexpr size_t KeptBytes = size_t{128} << 20;
    /// <summary>
    /// The slots' bytes in all while a load's fill runs (<see cref="Grow"/>): the A10's loads came out 0.45 s shorter than
    /// at <see cref="KeptBytes"/>, and 512 MB gave only 0.19 s more.
    /// </summary>
    static constexpr size_t LoadBytes = size_t{256} << 20;
    /// <summary>The most bytes <see cref="SetBytes"/> takes (a transfer buffer is at most 4 GB).</summary>
    static constexpr size_t MostBytes = size_t{2048} << 20;
    /// <summary>The slots kept from the fill and the ground (<see cref="MCKind"/>).</summary>
    static constexpr size_t NearSlots = 4;
    /// <summary>
    /// The share of the slots past the near ones the fill may hold unsent (staged by its readers, not yet recorded by the
    /// game's thread): the rest stays the frames' and the ground's, which the game's thread alone frees.
    /// </summary>
    static constexpr size_t FillShareNumerator = 1;
    static constexpr size_t FillShareDenominator = 2;
    /// <summary>Where uploads start in a lease (the strictest boundary any backend asks of a texture copy).</summary>
    static constexpr size_t Alignment = 512;
    /// <summary>How often a waiting writer looks at the fences again (the GPU says nothing as one signals).</summary>
    static constexpr std::chrono::microseconds WaitPoll{250};
    /// <summary>
    /// How often the fill's waiting reader looks at them: it isn't in a hurry, and the game's thread wakes it as it finds
    /// copies done at each frame's start (<see cref="Reclaim"/>).
    /// </summary>
    static constexpr std::chrono::microseconds FillWaitPoll{2000};
    /// <summary>
    /// The least bytes a batch of copies carries for its time from submission to fence to count as the copy speed's
    /// (<see cref="CopyBytesPerSecond"/>): a small batch's time is mostly the queue's.
    /// </summary>
    static constexpr size_t TimedLeastBytes = size_t{4} << 20;

    /// <summary>Who takes slots, for the tallies and the slots kept (<see cref="NearSlots"/>).</summary>
    enum class MCKind : uint8_t
    {
        /// <summary>A frame's own uploads, by the game's thread and the render thread: never waits, may take any slot.</summary>
        Frame,
        /// <summary>The read-ahead's near frames (those soon drawn): may take any slot.</summary>
        Near,
        /// <summary>The whole mission going up (SPEC-memory.md's plan, item 2): leaves NearSlots free, holds FillShare unsent.</summary>
        Fill,
        /// <summary>The ground's tiles and cells: leaves NearSlots free.</summary>
        Ground,
    };

    static constexpr size_t KindCount = 4;

    /// <summary>How <see cref="Take"/> waits when no slots are free.</summary>
    enum class MCWait : uint8_t
    {
        /// <summary>Until slots are free or it's stopped: a reader's.</summary>
        Free,
        /// <summary>
        /// Only while copies already sent may free them (the game's thread's at a load, whose own leases nobody else
        /// frees): none once nothing sent is left to run.
        /// </summary>
        Sent,
    };

    /// <summary>The kind's name in the frame log's counts ("frame", "near", "fill", "ground").</summary>
    static std::string_view KindName(MCKind kind);

    /// <summary>The memory behind the slots, and the fences: SDL's (<see cref="Make"/>), or a test's.</summary>
    class MCBacking
    {
    public:
        virtual ~MCBacking() = default;
        /// <summary>The slots' memory, mapped for good.</summary>
        virtual std::span<uint8_t> Memory() = 0;
        /// <summary>The transfer buffer the copies read (none for a test's).</summary>
        virtual MCOptionalRef<SDL_GPUTransferBuffer> Buffer() = 0;
        /// <summary>Whether <paramref name="fence"/>'s commands have run.</summary>
        virtual bool Done(SDL_GPUFence& fence) = 0;
        /// <summary>Waits until <paramref name="fence"/>'s commands have run; false if it can't.</summary>
        virtual bool Wait(SDL_GPUFence& fence) = 0;
        /// <summary>Gives <paramref name="fence"/> back (only once it's done).</summary>
        virtual void Release(SDL_GPUFence& fence) = 0;
        /// <summary>What's written is made the GPU's to read, before copies that read it are recorded.</summary>
        virtual void Flush() = 0;
    };

    /// <summary>
    /// A run of slots taken by one writer: room for its uploads (<see cref="Place"/>), until it's sent with the fence
    /// of the commands copying from it (<see cref="MCUploadSlots::Send"/>). One shared by uploads recorded over several
    /// frames (the fill's) is sent with each, and its slots free once it's gone and every fence has signalled. One never
    /// sent gives its slots back as it goes.
    /// </summary>
    class MCLease
    {
    public:
        MCLease() = default;
        MCLease(MCLease&& other) noexcept;
        MCLease& operator=(MCLease&& other) noexcept;
        MCLease(const MCLease&) = delete;
        MCLease& operator=(const MCLease&) = delete;
        ~MCLease();

        explicit operator bool() const { return _Pool != nullptr; }

        /// <summary>Its first byte's offset in the slots' buffer, and its bytes.</summary>
        size_t Offset() const;
        size_t Size() const;
        /// <summary>The bytes placed in it so far (<see cref="Place"/>), from its start.</summary>
        size_t Used() const { return _Used; }
        /// <summary>Its first slot and its slots.</summary>
        uint32_t First() const { return _First; }
        uint32_t Count() const { return _Count; }
        MCKind Kind() const { return _Kind; }
        /// <summary>The transfer buffer its copies read from.</summary>
        SDL_GPUTransferBuffer* Buffer() const;

        /// <summary>
        /// Room for <paramref name="bytes"/> after what's placed, on an <see cref="Alignment"/> boundary: its offset in
        /// the slots' buffer; none when they don't fit.
        /// </summary>
        std::optional<size_t> Place(size_t bytes);

        /// <summary>The slots' memory at <paramref name="offset"/> (in the buffer, inside this lease), <paramref name="bytes"/> long.</summary>
        std::span<uint8_t> At(size_t offset, size_t bytes) const;

    private:
        friend class MCUploadSlots;

        /// <summary>Gives its slots back (free once every fence sent with them has signalled), if it has any.</summary>
        void Drop();

        std::shared_ptr<MCUploadSlots> _Pool;
        uint32_t _First = 0;
        uint32_t _Count = 0;
        size_t _Used = 0;
        MCKind _Kind = MCKind::Frame;
    };

    /// <summary>Makes the memory for more slots, <c>size_t</c> bytes of them (whole slots), or says why not.</summary>
    using MCMakeBacking = std::function<std::expected<std::unique_ptr<MCBacking>, std::string>(size_t)>;

    /// <summary>The slots of <paramref name="bytes"/> in all (whole slots) on <paramref name="device"/>, or why not.</summary>
    static std::expected<std::shared_ptr<MCUploadSlots>, std::string> Make(const std::shared_ptr<SDL_GPUDevice>& device,
                                                                           size_t bytes);

    /// <summary>
    /// The slots over <paramref name="backing"/>'s memory (the tests'), growing with memory <paramref name="more"/> makes
    /// (none: they don't grow).
    /// </summary>
    static std::shared_ptr<MCUploadSlots> MakeWith(std::unique_ptr<MCBacking> backing, MCMakeBacking more = {});

    /// <summary>The slots' bytes made at the start: <see cref="SetBytes"/>'s when set, else <see cref="KeptBytes"/>.</summary>
    static size_t StartBytes();

    /// <summary>
    /// The slots' bytes while a load's fill runs: <see cref="SetBytes"/>'s when set (a timing run's size holds
    /// throughout), else <see cref="LoadBytes"/>.
    /// </summary>
    static size_t LoadBytesNow();

    /// <summary>
    /// Port-only, for timing runs (<c>-hdslots &lt;MB&gt;</c>; SPEC-memory.md: timed at 128, 256 and 512 MB): the slots'
    /// bytes, loading or not (none: <see cref="KeptBytes"/>, and <see cref="LoadBytes"/> while loading). Read as the GPU
    /// starts.
    /// </summary>
    static void SetBytes(std::optional<size_t> bytes);

    /// <summary>Waits for every sent lease's copies, then lets the memory go.</summary>
    ~MCUploadSlots();

    MCUploadSlots(const MCUploadSlots&) = delete;
    MCUploadSlots& operator=(const MCUploadSlots&) = delete;
    /// <summary>Slots in <paramref name="backing"/>, more made by <paramref name="more"/> (MakeWith's).</summary>
    MCUploadSlots(Key, std::unique_ptr<MCBacking> backing, MCMakeBacking more);

    /// <summary>How many slots there are (those let go once their copies have run among them), and their bytes in all.</summary>
    size_t Slots() const;
    size_t Bytes() const { return Slots() * SlotBytes; }

    /// <summary>
    /// Port-only (SPEC-memory.md's upload slots, under the loading bar): the slots grown to <paramref name="bytes"/> in
    /// all (whole slots), the ones past those made at the start in a buffer of their own; those let go and not yet gone
    /// (<see cref="Shrink"/>) are taken again. Why not, when that memory can't be made (the slots stay as they were).
    /// The game's thread's.
    /// </summary>
    std::expected<void, std::string> Grow(size_t bytes);

    /// <summary>
    /// The slots past those made at the start (<see cref="Grow"/>) taken no more; their buffer goes once their copies
    /// have run and no lease holds them (<see cref="Reclaim"/>). The game's thread's.
    /// </summary>
    void Shrink();
    /// <summary>The most slots <paramref name="kind"/> may hold at once (unsent, for the fill).</summary>
    size_t MostFor(MCKind kind) const;

    /// <summary>
    /// A lease of room for <paramref name="bytes"/> (whole slots, neighbours), taken now if slots are free (the fences
    /// of those sent are looked at first), else none: the game's thread's and the render thread's, which never wait.
    /// None also for more than the slots hold.
    /// </summary>
    std::optional<MCLease> TryTake(MCKind kind, size_t bytes);

    /// <summary>
    /// As <see cref="TryTake"/>, waiting as <paramref name="wait"/> says until slots are free (the copies sent before have
    /// run), for <paramref name="most"/> at most. None for more than the slots <paramref name="kind"/> may take hold, once
    /// <paramref name="stop"/> is asked, or once <paramref name="most"/> has gone.
    /// </summary>
    std::optional<MCLease> Take(MCKind kind, size_t bytes, std::stop_token stop = {}, MCWait wait = MCWait::Free,
                                std::chrono::steady_clock::duration most = std::chrono::steady_clock::duration::max());

    /// <summary>
    /// <paramref name="leases"/>' copies, and some of <paramref name="shared"/>'s, are recorded on commands just
    /// submitted with <paramref name="fence"/> (taken over: given back once done): their slots are free again once it has
    /// signalled (a shared lease's once it's gone too). The leases are emptied; the shared ones kept. A fence of none (its
    /// submission failed) gives the leases back now: no copy of theirs will run.
    /// </summary>
    void Send(std::span<MCLease> leases, std::span<const std::shared_ptr<const MCLease>> shared, SDL_GPUFence* fence,
              size_t bytes = 0);
    void Send(std::span<MCLease> leases, SDL_GPUFence* fence) { Send(leases, {}, fence); }

    /// <summary>
    /// Frees the slots of every lease sent whose fence has signalled, and lets go of the buffer of slots let go
    /// (<see cref="Shrink"/>) once they're all free; how many slots it freed. The game's thread's.
    /// </summary>
    size_t Reclaim();

    /// <summary>Waits for every lease sent to be done, and frees their slots; false if a fence can't be waited for.</summary>
    bool WaitForAll();

    /// <summary>What writes made seen by the GPU before copies of them are recorded (a no-op on shared memory).</summary>
    void Flush();

    /// <summary>
    /// Port-only (SPEC-memory.md: the copy speed measured under the loading bar): the fastest bytes a second seen from a
    /// batch's submission to its fence being seen signalled, over batches of <see cref="TimedLeastBytes"/> or more; 0
    /// before one. Fences are looked at as writers take slots, so it's a little under the GPU's.
    /// </summary>
    double CopyBytesPerSecond() const;

    /// <summary>A kind's takes since the slots were made.</summary>
    struct MCKindTally
    {
        /// <summary>Leases taken, and those refused (TryTake found none free, or Take gave up).</summary>
        int64_t Takes = 0;
        int64_t Refused = 0;
        /// <summary>Takes that waited for slots (Take), and their time in all.</summary>
        int64_t Waits = 0;
        std::chrono::nanoseconds Waited{};
        /// <summary>Slots it holds now (taken, not given back).</summary>
        size_t Held = 0;
    };

    struct MCTally
    {
        std::array<MCKindTally, KindCount> Kinds{};
        /// <summary>Slots not free (taken, or sent and not yet done), and the most at once.</summary>
        size_t InUse = 0;
        size_t PeakInUse = 0;
        /// <summary>Sent batches whose fences haven't been seen signalled, and those sent in all.</summary>
        size_t InFlight = 0;
        int64_t Sent = 0;
        /// <summary>The bytes sent in batches whose time was taken (<see cref="CopyBytesPerSecond"/>), and that time.</summary>
        int64_t TimedBytes = 0;
        std::chrono::nanoseconds TimedTime{};

        const MCKindTally& Of(MCKind kind) const { return Kinds[static_cast<size_t>(kind)]; }
    };

    MCTally Tally() const;

private:
    /// <summary>A slot: whether a lease holds it, and how many batches sent with it haven't been seen done.</summary>
    struct MCSlot
    {
        bool Leased = false;
        uint32_t Sends = 0;
    };

    /// <summary>Leases sent with one fence: their runs of slots (first, count), when, and the bytes their copies carry.</summary>
    struct MCBatch
    {
        SDL_GPUFence* Fence = nullptr;
        std::vector<std::pair<uint32_t, uint32_t>> Runs;
        std::chrono::steady_clock::time_point SentAt;
        size_t Bytes = 0;
    };

    /// <summary>The lease's slots no longer its (free once no batch sent with them is in flight).</summary>
    void GiveBack(MCLease& lease);

    /// <summary>Takes a run for <paramref name="bytes"/>, the lock held; none if no run of free slots fits.</summary>
    std::optional<MCLease> TakeLocked(MCKind kind, size_t bytes);
    /// <summary><see cref="Reclaim"/> with the lock held.</summary>
    size_t ReclaimLocked();
    /// <summary><see cref="MostFor"/> with the lock held.</summary>
    size_t MostForLocked(MCKind kind) const;
    /// <summary>The slots a lease of <paramref name="bytes"/> takes.</summary>
    static uint32_t SlotsFor(size_t bytes);
    /// <summary>Whether slot <paramref name="at"/> is free.</summary>
    bool Free(uint32_t at) const { return !_States[at].Leased && _States[at].Sends == 0; }
    /// <summary>Whether slot <paramref name="at"/> is free and may be taken (not let go: <see cref="Shrink"/>).</summary>
    bool FreeToTake(uint32_t at) const { return at < _Takeable && Free(at); }
    /// <summary>Counts the slots' states again into the tally (the lock held).</summary>
    void Recount();
    /// <summary>The buffer of slots let go, once none of them is held or in flight (the lock held): its slots gone.</summary>
    std::unique_ptr<MCBacking> TakeLetGoLocked();

    std::unique_ptr<MCBacking> _Backing;
    SDL_GPUTransferBuffer* _Buffer = nullptr;
    std::span<uint8_t> _Memory;
    /// <summary>How many slots are made at the start, in the first buffer (fixed): those past it are the grown ones.</summary>
    size_t _StartSlots = 0;
    /// <summary>Makes memory for grown slots (none: they don't grow); the grown slots' memory and their buffer.</summary>
    MCMakeBacking _MakeMore;
    std::unique_ptr<MCBacking> _More;
    SDL_GPUTransferBuffer* _MoreBuffer = nullptr;
    std::span<uint8_t> _MoreMemory;
    mutable std::mutex _Lock;
    std::condition_variable _Freed;
    std::vector<MCSlot> _States;
    /// <summary>The slots that may be taken: all, or those made at the start once the grown ones are let go.</summary>
    size_t _Takeable = 0;
    /// <summary>The slots free to take (FreeToTake).</summary>
    size_t _FreeSlots = 0;
    std::vector<MCBatch> _Sent;
    MCTally _Tally;
    double _CopyBytesPerSecond = 0.0;
};

/// <summary>
/// Port-only: one writer's uploads for one command buffer, staged in the upload slots (<see cref="MCUploadSlots"/>):
/// each in the batch's last lease while it fits, else a new lease; past the slots (none free for a writer that doesn't
/// wait, or bigger than they hold) in memory of its own, gone once its copy has run, which the frame log counts. Its
/// copies go up on a command buffer of their own (<see cref="Submit"/>), submitted before the commands that read them
/// (the GPU runs one queue in order), whose fence frees the slots. It may carry copies from leases staged elsewhere
/// (<see cref="Carry"/>: the fill's), sent with its fence too.
/// </summary>
class MCUploadBatch
{
public:
    /// <summary>Where a staged upload's bytes go, and where its copy reads them.</summary>
    struct MCStaged
    {
        /// <summary>Its memory, to be written in full (and only written); empty when nothing could hold it.</summary>
        std::span<uint8_t> Bytes;
        SDL_GPUTransferBuffer* Buffer = nullptr;
        uint32_t Offset = 0;
    };

    MCUploadBatch() = default;
    MCUploadBatch(std::shared_ptr<SDL_GPUDevice> device, std::shared_ptr<MCUploadSlots> slots,
                  MCUploadSlots::MCKind kind);
    MCUploadBatch(MCUploadBatch&&) noexcept = default;
    MCUploadBatch& operator=(MCUploadBatch&&) noexcept = default;
    MCUploadBatch(const MCUploadBatch&) = delete;
    MCUploadBatch& operator=(const MCUploadBatch&) = delete;

    /// <summary>
    /// Room for <paramref name="bytes"/>: a new lease takes at least <paramref name="leaseBytes"/>; when the slots have
    /// none free, <paramref name="wait"/> waits for those sent (the ground's at a load), else memory of its own (of
    /// <paramref name="leaseBytes"/> at least, the next uploads placed after it).
    /// </summary>
    MCStaged Stage(size_t bytes, size_t leaseBytes, bool wait);

    /// <summary>
    /// Carries copies from <paramref name="lease"/> (staged by another writer: the fill's readers), <paramref name="bytes"/>
    /// of them: it's sent with this batch's fence and kept by it until then.
    /// </summary>
    void Carry(std::shared_ptr<const MCUploadSlots::MCLease> lease, size_t bytes);

    /// <summary>Whether it holds anything staged or carried (since it was made, submitted or cleared).</summary>
    bool Empty() const { return _Staged == 0 && _Carried.empty(); }
    /// <summary>The bytes staged, and carried.</summary>
    size_t StagedBytes() const { return _Staged; }
    size_t CarriedBytes() const { return _CarriedBytes; }
    /// <summary>The leases it holds, and the uploads past the slots (their buffers of its own).</summary>
    size_t Leases() const { return _Leases.size(); }
    size_t PastSlots() const { return _Own.size(); }

    /// <summary>
    /// Has <paramref name="record"/> record the staged uploads' copies (and any copy between textures that goes in
    /// order with them) in a copy pass, on a command buffer of its own, submitted now with a fence that frees the slots
    /// once they've run; empty after.
    /// </summary>
    template <typename Record> std::expected<void, std::string> Submit(Record&& record)
    {
        SDL_GPUCommandBuffer* const commands = Begin();

        if (commands == nullptr)
        {
            Clear();
            return std::unexpected(std::string("SDL_AcquireGPUCommandBuffer (the uploads): ") + SDL_GetError());
        }

        SDL_GPUCopyPass* const pass = SDL_BeginGPUCopyPass(commands);
        record(*pass);
        SDL_EndGPUCopyPass(pass);
        return End(*commands);
    }

    /// <summary>Gives back everything staged, unsent (no copy of it will run), and lets go of what it carried.</summary>
    void Clear();

private:
    /// <summary>A transfer buffer of its own (past the slots), mapped, its size and the bytes placed in it.</summary>
    struct MCOwn
    {
        std::shared_ptr<SDL_GPUTransferBuffer> Buffer;
        uint8_t* Memory = nullptr;
        size_t Size = 0;
        size_t Used = 0;
    };

    /// <summary>What's written made the GPU's to read, and a command buffer for the copies.</summary>
    SDL_GPUCommandBuffer* Begin();
    /// <summary>Submits <paramref name="commands"/> with a fence, which the slots take with the leases.</summary>
    std::expected<void, std::string> End(SDL_GPUCommandBuffer& commands);
    /// <summary>Room in the last buffer of its own, or a new one of <paramref name="size"/> at least; empty when none can be made.</summary>
    MCStaged StageOwn(size_t bytes, size_t size);

    std::shared_ptr<SDL_GPUDevice> _Device;
    std::shared_ptr<MCUploadSlots> _Slots;
    MCUploadSlots::MCKind _Kind = MCUploadSlots::MCKind::Frame;
    std::vector<MCUploadSlots::MCLease> _Leases;
    /// <summary>Leases staged elsewhere whose copies it carries, and their bytes.</summary>
    std::vector<std::shared_ptr<const MCUploadSlots::MCLease>> _Carried;
    size_t _CarriedBytes = 0;
    /// <summary>Uploads past the slots: transfer buffers of their own, let go once submitted (SDL keeps them until their
    /// copies have run).</summary>
    std::vector<MCOwn> _Own;
    /// <summary>What nothing could hold is written here, and not sent.</summary>
    std::vector<uint8_t> _Lost;
    size_t _Staged = 0;
};
