#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <thread>

/// <summary>
/// Port-only (the memory ledger): a figure of the game's memory that takes a walk of its address space to count (the
/// GPU's mapped pages on Windows, the heap's regions on macOS), walked once a second on a thread of its own from the
/// first time it's asked (a frame log's first line): a walk took 1.55 ms on the A10 (17 Nov), too long for the game's
/// thread.
/// </summary>
class MCMemoryWalk
{
public:
    /// <summary>A walk: the bytes it counts.</summary>
    using MCCount = uint64_t (*)();

    explicit MCMemoryWalk(MCCount count) : _Count(count) {}

    /// <summary>The last walk's bytes (0 until the first is done), the walks started if they weren't.</summary>
    uint64_t Last()
    {
        std::call_once(_Started, [this] { _Walker = std::jthread([this](std::stop_token stop) { Walk(stop); }); });
        return _Bytes.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::chrono::seconds WalkEvery{1};

    void Walk(const std::stop_token& stop)
    {
        while (!stop.stop_requested())
        {
            _Bytes.store(_Count(), std::memory_order_relaxed);
            std::unique_lock held(_Lock);
            _Wake.wait_for(held, stop, WalkEvery, [] { return false; });
        }
    }

    MCCount _Count;
    std::atomic<uint64_t> _Bytes = 0;
    std::once_flag _Started;
    std::mutex _Lock;
    std::condition_variable_any _Wake;
    /// <summary>Last: stopped and joined before the rest it reads goes.</summary>
    std::jthread _Walker;
};
