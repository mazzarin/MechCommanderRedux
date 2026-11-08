#include "stdafx.h"
#include "lib/MCThreadKeeper.h"

#include "platform/MCNeverDestroyed.h"

MCThreadKeeper::~MCThreadKeeper()
{
    JoinAll();
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
    std::jthread reaper;

    {
        const std::scoped_lock lock(_Lock);
        reaper = std::move(_Reaper);
    }

    // Asked to stop, it joins every thread still queued first.
    if (reaper.joinable())
    {
        reaper.request_stop();
        reaper.join();
    }
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
    }
}
