#include "stdafx.h"
#include "lib/MCThreadKeeper.h"

#include "platform/MCNeverDestroyed.h"

MCThreadKeeper::~MCThreadKeeper()
{
    JoinAll();
    // The reaper, asked to stop by its jthread's destruction, finds none queued and returns.
}

MCThreadKeeper& MCThreadKeeper::Process()
{
    static MCNeverDestroyed<MCThreadKeeper> keeper;
    return *keeper;
}

void MCThreadKeeper::Keep(std::thread thread)
{
    if (!thread.joinable())
    {
        return;
    }

    const std::scoped_lock lock(_Lock);
    _Threads.push_back(std::move(thread));

    if (!_Reaper.joinable())
    {
        _Reaper = std::jthread([this](std::stop_token stop) { Reap(std::move(stop)); });
    }

    _Kept.notify_all();
}

void MCThreadKeeper::JoinAll()
{
    // The reaper goes on joining; this waits until none is queued or being joined, those kept meanwhile too (one reaper
    // only, so no thread kept before the call is joined by another that this could miss).
    std::unique_lock lock(_Lock);
    _Drained.wait(lock, [this] { return _Threads.empty() && _Joining == 0; });
}

size_t MCThreadKeeper::Kept()
{
    const std::scoped_lock lock(_Lock);
    return _Threads.size() + _Joining;
}

void MCThreadKeeper::Reap(std::stop_token stop)
{
    std::unique_lock lock(_Lock);

    for (;;)
    {
        _Kept.wait(lock, stop, [this] { return !_Threads.empty(); });

        // Woken with none queued: asked to stop.
        if (_Threads.empty())
        {
            return;
        }

        std::thread next = std::move(_Threads.front());
        _Threads.pop_front();
        _Joining++;
        lock.unlock();
        next.join();
        lock.lock();
        _Joining--;

        if (_Threads.empty() && _Joining == 0)
        {
            _Drained.notify_all();
        }
    }
}
