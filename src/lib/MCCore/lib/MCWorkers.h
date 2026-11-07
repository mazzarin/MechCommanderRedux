#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <system_error>
#include <thread>
#include <vector>

/// <summary>
/// Port-only: a few threads that help one thread (the game's) through work split into parts, such as the HD weather's
/// marks. <see cref="Run"/> hands the parts out one at a time, first come first served, to its helpers and the calling
/// thread alike, and returns once every part has returned: what the parts read stays as it was for as long as they run
/// (nothing else runs on the calling thread meanwhile), and each part writes only what is its own, so they need no
/// locks. Where a part's result goes is set by its number, never by which thread ran it or when, so the results are the
/// same as running the parts in order on one thread.
/// </summary>
/// <remarks>Threads: <see cref="Run"/> from one thread at a time; the helpers start with the first Run.</remarks>
class MCWorkers
{
public:
    /// <summary>Workers with <paramref name="helpers"/> threads of their own (0: the calling thread does it all).</summary>
    explicit MCWorkers(size_t helpers);
    ~MCWorkers();
    MCWorkers(const MCWorkers&) = delete;
    MCWorkers& operator=(const MCWorkers&) = delete;

    /// <summary>
    /// Runs <paramref name="work"/> once for each part, 0 to <paramref name="parts"/> - 1, on the helpers and this
    /// thread in no set order, and returns once every one has returned. The first exception a part throws is thrown
    /// here then (the other parts run as ever).
    /// </summary>
    void Run(size_t parts, const std::function<void(size_t)>& work);

    /// <summary>How many threads of their own the workers have.</summary>
    size_t Helpers() const { return _HelperCount; }

    /// <summary>
    /// The helpers the game's thread is given: half the machine's threads, less this one, at most
    /// <paramref name="most"/> (0 on a machine of two threads or fewer).
    /// </summary>
    static size_t HelpersFor(size_t most);

private:
    /// <summary>A helper: waits for each batch of parts and takes its share of it, until asked to stop.</summary>
    void Help(std::stop_token stop);

    /// <summary>
    /// Takes the batch's parts that are left one at a time and runs <paramref name="work"/> on each, until none are
    /// left (<paramref name="parts"/> in all), noting the first exception.
    /// </summary>
    void TakeParts(const std::function<void(size_t)>& work, size_t parts);

    size_t _HelperCount = 0;
    /// <summary>Guards what follows, but for the counts the parts take and finish by.</summary>
    std::mutex _Lock;
    /// <summary>Told of every batch handed out, and of the workers' end.</summary>
    std::condition_variable_any _Handed;
    /// <summary>Told when the batch's last part is done or a helper leaves it.</summary>
    std::condition_variable _Finished;
    /// <summary>The batch's work while it runs (none between batches), its number of parts and its number.</summary>
    std::optional<std::reference_wrapper<const std::function<void(size_t)>>> _Work;
    size_t _Parts = 0;
    uint64_t _Batch = 0;
    /// <summary>The helpers inside the batch now: Run waits for them to leave before its work goes.</summary>
    size_t _Inside = 0;
    /// <summary>The batch's first exception.</summary>
    std::exception_ptr _Error;
    /// <summary>The next part to take, and the parts not done yet.</summary>
    std::atomic<size_t> _Next{0};
    std::atomic<size_t> _Left{0};
    /// <summary>The helpers' threads; last, so they stop before the rest goes.</summary>
    std::vector<std::jthread> _Threads;
};
