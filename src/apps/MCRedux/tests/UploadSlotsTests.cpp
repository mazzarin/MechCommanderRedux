#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "platform/MCRenderer.h"
#include "platform/MCUploadSlots.h"
#include "platform/MCVulkanRenderer.h"

#include <atomic>
#include <map>
#include <thread>

// Port-only (SPEC-memory.md's "A frame's path", row 5): the upload slots every upload to the GPU goes up through
// (platform/MCUploadSlots.h): a slot is used again only once its copy's fence has signalled, a reader waits for free
// slots and the game's thread never does, the fill and the ground leave the near slots free; and the game's frames
// drawn through them.

namespace
{
    constexpr size_t Slot = MCUploadSlots::SlotBytes;
    using MCKind = MCUploadSlots::MCKind;

    /// <summary>Fences the test signals, over memory of its own.</summary>
    class MCTestBacking final : public MCUploadSlots::MCBacking
    {
    public:
        /// <summary>The fences, by the number each stands for, and whether each has signalled.</summary>
        struct MCFences
        {
            std::mutex Lock;
            std::map<uintptr_t, bool> Done;
            std::vector<uintptr_t> Released;
        };

        MCTestBacking(size_t slots, std::shared_ptr<MCFences> fences)
            : _Memory(slots * MCUploadSlots::SlotBytes), _Fences(std::move(fences))
        {
        }

        std::span<uint8_t> Memory() override { return _Memory; }
        SDL_GPUTransferBuffer* Buffer() override { return nullptr; }

        bool Done(SDL_GPUFence* fence) override
        {
            std::scoped_lock lock(_Fences->Lock);
            return _Fences->Done[Number(fence)];
        }

        bool Wait(SDL_GPUFence* fence) override
        {
            std::scoped_lock lock(_Fences->Lock);
            _Fences->Done[Number(fence)] = true;
            return true;
        }

        void Release(SDL_GPUFence* fence) override
        {
            std::scoped_lock lock(_Fences->Lock);
            _Fences->Released.push_back(Number(fence));
        }

        void Flush() override {}

        static SDL_GPUFence* Fence(uintptr_t number) { return reinterpret_cast<SDL_GPUFence*>(number); }
        static uintptr_t Number(SDL_GPUFence* fence) { return reinterpret_cast<uintptr_t>(fence); }

    private:
        std::vector<uint8_t> _Memory;
        std::shared_ptr<MCFences> _Fences;
    };

    /// <summary>Slots of the test's, <paramref name="slots"/> of them, and their fences.</summary>
    struct MCTestSlots
    {
        std::shared_ptr<MCTestBacking::MCFences> Fences = std::make_shared<MCTestBacking::MCFences>();
        std::shared_ptr<MCUploadSlots> Slots;

        explicit MCTestSlots(size_t slots)
        {
            Slots = MCUploadSlots::MakeWith(std::make_unique<MCTestBacking>(slots, Fences));
        }

        /// <summary>Signals fence <paramref name="number"/>, as the GPU does once the copies before it have run.</summary>
        void Signal(uintptr_t number)
        {
            std::scoped_lock lock(Fences->Lock);
            Fences->Done[number] = true;
        }

        /// <summary>Sends <paramref name="lease"/> with fence <paramref name="number"/>.</summary>
        void Send(MCUploadSlots::MCLease& lease, uintptr_t number)
        {
            {
                std::scoped_lock lock(Fences->Lock);
                Fences->Done[number] = false;
            }

            Slots->Send(std::span(&lease, 1), MCTestBacking::Fence(number));
        }
    };
}

TEST_CASE("upload slots: 128 MB at the least, 256 MB from 24 GB, or as set")
{
    constexpr uint64_t GB = uint64_t{1} << 30;
    constexpr size_t MB = size_t{1} << 20;
    MCUploadSlots::SetBytes(std::nullopt);
    CHECK_EQ(MCUploadSlots::BytesFor(8 * GB), 128 * MB);
    CHECK_EQ(MCUploadSlots::BytesFor(16 * GB), 128 * MB);
    // A 24 GB PC reports a little less (the firmware's).
    CHECK_EQ(MCUploadSlots::BytesFor(24 * GB - 512 * MB), 256 * MB);
    CHECK_EQ(MCUploadSlots::BytesFor(64 * GB), 256 * MB);
    MCUploadSlots::SetBytes(512 * MB);
    CHECK_EQ(MCUploadSlots::BytesFor(8 * GB), 512 * MB);
    // Never fewer slots than the near ones and one more, nor more than a transfer buffer holds.
    MCUploadSlots::SetBytes(1);
    CHECK_EQ(MCUploadSlots::BytesFor(8 * GB), (MCUploadSlots::NearSlots + 1) * Slot);
    MCUploadSlots::SetBytes(std::nullopt);
    CHECK_EQ(MCUploadSlots::BytesFor(8 * GB), 128 * MB);
}

TEST_CASE("upload slots: a slot is never used again before its copy's fence has signalled")
{
    MCTestSlots test(8);
    MCUploadSlots& slots = *test.Slots;
    REQUIRE_EQ(slots.Slots(), size_t{8});

    // Every slot taken, then sent with two fences.
    std::optional<MCUploadSlots::MCLease> first = slots.TryTake(MCKind::Frame, 3 * Slot);
    std::optional<MCUploadSlots::MCLease> second = slots.TryTake(MCKind::Frame, 5 * Slot);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK_EQ(first->Count(), 3u);
    CHECK_EQ(second->First(), 3u);
    const uint32_t firstSlot = first->First();
    test.Send(*first, 1);
    test.Send(*second, 2);
    CHECK(!*first);
    CHECK_EQ(slots.Tally().InFlight, size_t{2});

    // Nothing is free while neither fence has signalled, however often the fences are looked at.
    for (int32_t look = 0; look < 3; ++look)
    {
        CHECK_EQ(slots.Reclaim(), size_t{0});
        CHECK(!slots.TryTake(MCKind::Frame, 1).has_value());
    }

    CHECK_EQ(slots.Tally().Of(MCKind::Frame).Refused, int64_t{3});

    // The second's copies done (a later submission's fence may signal first): only its slots come back.
    test.Signal(2);
    CHECK_EQ(slots.Reclaim(), size_t{5});
    CHECK(!slots.TryTake(MCKind::Frame, 6 * Slot).has_value());
    std::optional<MCUploadSlots::MCLease> again = slots.TryTake(MCKind::Frame, 5 * Slot);
    REQUIRE(again.has_value());
    CHECK_EQ(again->First(), 3u);
    again.reset();

    // The first's: its slots are the first again, and both fences were given back once, after they signalled.
    test.Signal(1);
    std::optional<MCUploadSlots::MCLease> reused = slots.TryTake(MCKind::Frame, 8 * Slot);
    REQUIRE(reused.has_value());
    CHECK_EQ(reused->First(), firstSlot);
    CHECK(test.Fences->Released == std::vector<uintptr_t>({2, 1}));
}

TEST_CASE("upload slots: a lease places its uploads on the copies' boundary, and one never sent gives them back")
{
    MCTestSlots test(6);
    MCUploadSlots& slots = *test.Slots;

    {
        std::optional<MCUploadSlots::MCLease> lease = slots.TryTake(MCKind::Frame, 10);
        REQUIRE(lease.has_value());
        CHECK_EQ(lease->Size(), Slot);
        const std::optional<size_t> a = lease->Place(10);
        const std::optional<size_t> b = lease->Place(1000);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK_EQ(*a, lease->Offset());
        CHECK_EQ(*b, lease->Offset() + MCUploadSlots::Alignment);
        CHECK_EQ(lease->At(*b, 1000).size(), size_t{1000});
        // What doesn't fit isn't placed.
        CHECK(!lease->Place(Slot).has_value());
        CHECK_EQ(slots.Tally().InUse, size_t{1});
    }

    CHECK_EQ(slots.Tally().InUse, size_t{0});
    CHECK_EQ(slots.Tally().PeakInUse, size_t{1});
    // A send whose submission failed (no fence) gives them back too: none of its copies will run.
    std::optional<MCUploadSlots::MCLease> failed = slots.TryTake(MCKind::Frame, 6 * Slot);
    REQUIRE(failed.has_value());
    slots.Send(std::span(&*failed, 1), nullptr);
    CHECK_EQ(slots.Tally().InUse, size_t{0});
    // More than the slots hold is refused at once, even waiting.
    CHECK(!slots.Take(MCKind::Near, 7 * Slot).has_value());
}

TEST_CASE("upload slots: the fill and the ground leave the near slots free for the frames and the near frames")
{
    const size_t count = MCUploadSlots::NearSlots + 4;
    MCTestSlots test(count);
    MCUploadSlots& slots = *test.Slots;
    std::vector<MCUploadSlots::MCLease> held;

    for (MCKind kind : {MCKind::Fill, MCKind::Ground})
    {
        while (std::optional<MCUploadSlots::MCLease> lease = slots.TryTake(kind, 1))
        {
            held.push_back(std::move(*lease));
        }
    }

    // The fill holds its share of the slots past the near ones at most; the ground the rest.
    CHECK_EQ(slots.Tally().Of(MCKind::Fill).Held, (count - MCUploadSlots::NearSlots) / 2);
    CHECK_EQ(held.size(), count - MCUploadSlots::NearSlots);
    CHECK(slots.Tally().Of(MCKind::Fill).Refused == 1 && slots.Tally().Of(MCKind::Ground).Refused == 1);

    // A reader of the fill asking for more than it may take at all isn't kept waiting.
    CHECK(!slots.Take(MCKind::Fill, (count - MCUploadSlots::NearSlots + 1) * Slot).has_value());

    // The frames' own and the near frames take the rest.
    std::optional<MCUploadSlots::MCLease> frame = slots.TryTake(MCKind::Frame, 2 * Slot);
    std::optional<MCUploadSlots::MCLease> near = slots.TryTake(MCKind::Near, (MCUploadSlots::NearSlots - 2) * Slot);
    CHECK(frame.has_value());
    CHECK(near.has_value());
    CHECK(!slots.TryTake(MCKind::Frame, 1).has_value());
}

TEST_CASE("upload slots: a reader waits for the copies sent before to run; the game's thread is refused at once")
{
    MCTestSlots test(MCUploadSlots::NearSlots + 2);
    MCUploadSlots& slots = *test.Slots;
    std::optional<MCUploadSlots::MCLease> all = slots.TryTake(MCKind::Frame, slots.Bytes());
    REQUIRE(all.has_value());
    test.Send(*all, 7);

    // The game's thread: refused, never waiting.
    const auto start = std::chrono::steady_clock::now();
    CHECK(!slots.TryTake(MCKind::Frame, Slot).has_value());
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50));
    CHECK_EQ(slots.Tally().Of(MCKind::Frame).Waits, int64_t{0});

    // A reader of the fill: waits until the fence signals, then has its slot.
    std::atomic<bool> taken = false;
    std::jthread reader(
        [&]
        {
            std::optional<MCUploadSlots::MCLease> lease = slots.Take(MCKind::Fill, Slot);
            taken = lease.has_value();
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!taken.load());
    test.Signal(7);
    reader.join();
    CHECK(taken.load());
    CHECK_EQ(slots.Tally().Of(MCKind::Fill).Waits, int64_t{1});
    CHECK(slots.Tally().Of(MCKind::Fill).Waited >= std::chrono::milliseconds(10));

    // A reader stopped while it waits gives up.
    std::optional<MCUploadSlots::MCLease> again = slots.TryTake(MCKind::Frame, slots.Bytes());
    REQUIRE(again.has_value());
    test.Send(*again, 8);
    std::stop_source stop;
    std::jthread stopped([&] { taken = slots.Take(MCKind::Fill, Slot, stop.get_token()).has_value(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    stop.request_stop();
    stopped.join();
    CHECK(!taken.load());
}

TEST_CASE(
    "upload slots: a lease shared by several copies is free once it's gone and every fence sent with it has signalled")
{
    MCTestSlots test(MCUploadSlots::NearSlots + 2);
    MCUploadSlots& slots = *test.Slots;
    std::optional<MCUploadSlots::MCLease> taken = slots.TryTake(MCKind::Fill, Slot);
    REQUIRE(taken.has_value());
    auto shared = std::make_shared<const MCUploadSlots::MCLease>(std::move(*taken));

    // Its uploads copied over two frames.
    for (uintptr_t number : {uintptr_t{1}, uintptr_t{2}})
    {
        std::scoped_lock lock(test.Fences->Lock);
        test.Fences->Done[number] = false;
    }

    {
        const std::array carried{shared};
        slots.Send({}, carried, MCTestBacking::Fence(1));
        slots.Send({}, carried, MCTestBacking::Fence(2));
    }

    CHECK_EQ(slots.Tally().InUse, size_t{1});

    // The first frame's copies done, then the lease gone: the second's still read it.
    test.Signal(1);
    CHECK_EQ(slots.Reclaim(), size_t{0});
    shared.reset();
    CHECK_EQ(slots.Tally().Of(MCKind::Fill).Held, size_t{0});
    CHECK_EQ(slots.Tally().InUse, size_t{1});
    test.Signal(2);
    CHECK_EQ(slots.Reclaim(), size_t{1});
    CHECK_EQ(slots.Tally().InUse, size_t{0});

    // Sent, its fence signalled, then gone: free as it goes.
    std::optional<MCUploadSlots::MCLease> takenAgain = slots.TryTake(MCKind::Fill, Slot);
    REQUIRE(takenAgain.has_value());
    auto again = std::make_shared<const MCUploadSlots::MCLease>(std::move(*takenAgain));
    {
        std::scoped_lock lock(test.Fences->Lock);
        test.Fences->Done[3] = false;
    }

    {
        const std::array carriedAgain{again};
        slots.Send({}, carriedAgain, MCTestBacking::Fence(3));
    }

    test.Signal(3);
    CHECK_EQ(slots.Reclaim(), size_t{0});
    again.reset();
    CHECK_EQ(slots.Tally().InUse, size_t{0});
}

TEST_CASE("upload slots: the game's thread at a load waits only for copies already sent, never for its own")
{
    MCTestSlots test(MCUploadSlots::NearSlots + 2);
    MCUploadSlots& slots = *test.Slots;

    // Every slot held by a lease not yet sent (a frame's staging): nothing sent can free one, so none at once.
    std::optional<MCUploadSlots::MCLease> unsent = slots.TryTake(MCKind::Frame, slots.Bytes());
    REQUIRE(unsent.has_value());
    const auto start = std::chrono::steady_clock::now();
    CHECK(!slots.Take(MCKind::Ground, Slot, {}, MCUploadSlots::MCWait::Sent).has_value());
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50));
    CHECK_EQ(slots.Tally().Of(MCKind::Ground).Refused, int64_t{1});

    // Sent: it waits for their copies.
    test.Send(*unsent, 4);
    std::atomic<bool> taken = false;
    std::jthread ground([&] { taken = slots.Take(MCKind::Ground, Slot, {}, MCUploadSlots::MCWait::Sent).has_value(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(!taken.load());
    test.Signal(4);
    ground.join();
    CHECK(taken.load());
}

TEST_CASE("upload slots: the copy speed is the fastest seen over batches large enough to time")
{
    MCTestSlots test(MCUploadSlots::NearSlots + 2);
    MCUploadSlots& slots = *test.Slots;
    CHECK_EQ(slots.CopyBytesPerSecond(), 0.0);

    // A small batch isn't timed.
    std::optional<MCUploadSlots::MCLease> small = slots.TryTake(MCKind::Frame, Slot);
    REQUIRE(small.has_value());
    test.Send(*small, 5);
    test.Signal(5);
    slots.Reclaim();
    CHECK_EQ(slots.CopyBytesPerSecond(), 0.0);

    std::optional<MCUploadSlots::MCLease> large = slots.TryTake(MCKind::Frame, Slot);
    REQUIRE(large.has_value());
    {
        std::scoped_lock lock(test.Fences->Lock);
        test.Fences->Done[6] = false;
    }

    slots.Send(std::span(&*large, 1), {}, MCTestBacking::Fence(6), MCUploadSlots::TimedLeastBytes);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    test.Signal(6);
    slots.Reclaim();
    const double speed = slots.CopyBytesPerSecond();
    CHECK(speed > 0.0);
    CHECK(speed <= static_cast<double>(MCUploadSlots::TimedLeastBytes) / 0.02);
    CHECK_EQ(slots.Tally().TimedBytes, static_cast<int64_t>(MCUploadSlots::TimedLeastBytes));
}

/// <summary>
/// Mission 1's load and battle drawn by the GPU through upload slots of the least a frame needs: the frames' uploads,
/// the presenter's and the draws never wait for a slot (past the slots instead when none are free), the ground's at
/// the load may; every slot sent comes back, and nothing is drawn the GPU can't.
/// </summary>
TEST_CASE_ISOLATED("game: the frames' uploads never wait for the upload slots, the ground's only at the load")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // A few slots: the load's ground and its first frames' pictures fill them.
    constexpr size_t FewSlots = MCUploadSlots::NearSlots + 4;
    MCUploadSlots::SetBytes(FewSlots * Slot);
    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(1));
    const auto renderer = std::dynamic_pointer_cast<MCVulkanRenderer>(MCRenderer::Hardware());

    if (renderer == nullptr || MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        std::cout << "  (skipped: no GPU renderer on this machine)\n";
        MCUploadSlots::SetBytes(std::nullopt);
        return;
    }

    REQUIRE(renderer->UploadSlots() != nullptr);
    CHECK_EQ(renderer->UploadSlots()->Slots(), FewSlots);

    for (int32_t frame = 0; frame < 5 * 15; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    const MCUploadSlots::MCTally tally = renderer->UploadSlots()->Tally();
    std::cout << std::format(
        "  frames' leases {} (refused {}), ground's {} (waited {}, {} ms), peak {} of {} slots\n",
        tally.Of(MCKind::Frame).Takes, tally.Of(MCKind::Frame).Refused, tally.Of(MCKind::Ground).Takes,
        tally.Of(MCKind::Ground).Waits,
        std::chrono::duration_cast<std::chrono::milliseconds>(tally.Of(MCKind::Ground).Waited).count(), tally.PeakInUse,
        FewSlots);
    CHECK(tally.Of(MCKind::Frame).Takes > 0);
    CHECK(tally.Of(MCKind::Ground).Takes > 0);
    CHECK_EQ(tally.Of(MCKind::Frame).Waits, int64_t{0});
    CHECK_EQ(tally.Of(MCKind::Near).Waits, int64_t{0});
    CHECK(tally.PeakInUse <= FewSlots);
    CHECK(renderer->Unsupported().empty());

    // Once the GPU has run what was sent, every slot is free again.
    CHECK(renderer->UploadSlots()->WaitForAll());
    CHECK_EQ(renderer->UploadSlots()->Tally().InFlight, size_t{0});
    MCUploadSlots::SetBytes(std::nullopt);
}
