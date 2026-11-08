#pragma once

// Only standard headers: the platforms' sockets (apps/MCRedux/MCWebSocketWin.cpp) are built without the precompiled
// header.
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>

/// <summary>
/// Port-only: takes the threads objects let go of as they're destroyed (a WebSocket's finishing its close, a request's
/// stopping) and joins them on a thread of its own, in the order kept, so the thread that destroys such an object (the
/// game's) never waits on the network. A thread kept owns what it uses (through a shared_ptr), so it can outlive its
/// object. The process's keeper (<see cref="Process"/>) is joined at quit (<see cref="JoinAll"/>).
/// </summary>
/// <remarks>Threads: any call from any thread but a kept one.</remarks>
class MCThreadKeeper
{
public:
    MCThreadKeeper() = default;
    /// <summary>Joins every thread kept (<see cref="JoinAll"/>).</summary>
    ~MCThreadKeeper();
    MCThreadKeeper(const MCThreadKeeper&) = delete;
    MCThreadKeeper& operator=(const MCThreadKeeper&) = delete;

    /// <summary>
    /// The process's keeper: never destroyed, as an object may let go of its thread while the process ends; the game's
    /// quit joins what it holds (<see cref="JoinAll"/>).
    /// </summary>
    static MCThreadKeeper& Process();

    /// <summary>Takes <paramref name="thread"/>, to join once it finishes, and returns at once (nothing kept for one not running).</summary>
    void Keep(std::thread thread);

    /// <summary>
    /// Waits until every thread kept is joined, those kept while it waits too, as long as they take (the game's quit).
    /// </summary>
    void JoinAll();

    /// <summary>The threads kept and not yet joined.</summary>
    size_t Kept();

private:
    /// <summary>The keeper's thread: joins each thread kept in turn, until asked to stop with none left.</summary>
    void Reap(std::stop_token stop);

    std::mutex _Lock;
    /// <summary>Told when a thread is kept (and when the keeper's thread is asked to stop).</summary>
    std::condition_variable_any _Kept;
    /// <summary>Told when the last thread kept has been joined: none queued, none being joined.</summary>
    std::condition_variable _Drained;
    std::deque<std::thread> _Threads;
    /// <summary>The thread being joined now (out of the queue, its join under way without the lock): 0 or 1.</summary>
    size_t _Joining = 0;
    /// <summary>The keeper's thread, started with the first thread kept, for the keeper's life; last, so it goes first.</summary>
    std::jthread _Reaper;
};
